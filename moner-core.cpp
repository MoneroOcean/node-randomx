// Copyright GNU GPLv3 (c) 2023-2025 MoneroOcean <support@moneroocean.stream>

#include "moner-core.h"

#include "3rdparty/fmt/core.h"
#include "backend/cpu/Cpu.h"
#include "crypto/cn/CnCtx.h"
#include "crypto/randomx/blake2/blake2.h"
#include "crypto/randomx/blake2/avx2/blake2b.h"
#include "crypto/rx/RxFix.h"
#include "3rdparty/argon2.h"

#include <chrono>

static const xmrig::ICpuInfo& ci = *xmrig::Cpu::info();
void (*rx_blake2b_compress)(blake2b_state* S, const uint8_t * block) = rx_blake2b_compress_integer;
int (*rx_blake2b)(void* out, size_t outlen, const void* in, size_t inlen) = rx_blake2b_default;

static inline unsigned char hf_hex2bin(const char c, bool& err) {
  if (c >= '0' && c <= '9')      return c - '0';
  else if (c >= 'a' && c <= 'f') return c - 'a' + 0xA;
  else if (c >= 'A' && c <= 'F') return c - 'A' + 0xA;
  err = true;
  return 0;
}

bool Core::hex2bin(const char* in, unsigned int len, unsigned char* out) {
  bool error = false;
  for (unsigned int i = 0; i < len; ++i, ++out, in += 2) {
    *out = (hf_hex2bin(*in, error) << 4) | hf_hex2bin(*(in + 1), error);
    if (error) return false;
  }
  return true;
}

std::vector<std::string> Core::tokenize(const std::string& str, const char delim) {
  std::vector<std::string> out;
  size_t start;
  size_t end = 0;
  while ((start = str.find_first_not_of(delim, end)) != std::string::npos) {
    end = str.find(delim, start);
    out.push_back(str.substr(start, end - start));
  }
  return out;
}

static inline char hf_bin2hex(const unsigned n) {
  if (n < 10) return '0' + n;
  return 'a' + (n - 10);
}

char* Core::hash_bin2hex(const uint8_t* const output, char* hash, const unsigned batch) const {
  char* hash0 = hash;
  for (unsigned i = 0, offset = batch * HASH_LEN; i != HASH_LEN; ++ i, ++ offset) {
    *hash++ = hf_bin2hex(output[offset] >> 4);
    *hash++ = hf_bin2hex(output[offset] & 0xF);
  }
  *hash = 0;
  return hash0;
}

char* Core::hash_bin2hex(char* const hash, const unsigned batch) const {
  return hash_bin2hex(m_output, hash, batch);
}

void Core::send_msg(const std::string key, const MessageValues& values) {
  static std::mutex mutex_message;
  mutex_message.lock();
  sendToNode(*m_progress, Message(key, values));
  mutex_message.unlock();
}

void Core::send_msg(const std::string& topic, const std::string& key, const std::string& value) {
  MessageValues values;
  if (!key.empty()) values[key] = value;
  send_msg(topic, values);
}

void Core::send_error(const std::string& str) {
  send_msg("error", "message", str);
}

void Core::send_result(const uint32_t nonce, const uint8_t* const output) {
  MessageValues values;
  char nonce_hex[sizeof(uint32_t)*2+1], hash_hex[HASH_LEN*2+1];
  snprintf(nonce_hex, sizeof(uint32_t)*2+1, "%08x", __builtin_bswap32(nonce));
  values["nonce"]     = nonce_hex;
  values["hash"]      = hash_bin2hex(output, hash_hex);
  values["pool_id"]   = m_pool_id;
  values["worker_id"] = m_worker_id;
  values["job_id"]    = m_job_id;
  send_msg("result", values);
}

void Core::send_last_nonce(const uint32_t nonce, const std::string& pool_id) {
  MessageValues result;
  result["nonce"]   = std::to_string(nonce);
  result["pool_id"] = pool_id;
  send_msg("last_nonce", result);
}

static void free_mem(void* const mem) {
#if defined(__ARM_ARCH) || defined(__aarch64__) || defined(__arm64__)
  free(mem);
#else
  _mm_free(mem);
#endif
}

void Core::free_memory(
  const bool is_batch_changed,
  const bool is_mem_size_changed,
  const bool is_free_cn,
  const bool is_free_rx
) {
  // m_thread_pool need to be deleted first if anything rx related is deleted
  if (is_batch_changed || is_mem_size_changed || is_free_rx) {
    // ++ m_job_ref is to stop rx threads if any
    if (m_thread_pool) { ++ m_job_ref; delete m_thread_pool; m_thread_pool = nullptr; }
    if (m_vm) {
      for (int i = 0; i != m_batch; ++ i) randomx_destroy_vm(m_vm[i]);
      delete [] m_vm; m_vm = nullptr;
    }
  }
  if (is_batch_changed || is_mem_size_changed) {
    if (m_lpads) { delete m_lpads; m_lpads = nullptr; }
  }
  if (is_batch_changed) {
    if (m_input_cn) { free_mem(m_input_cn); m_input_cn = nullptr; }
    if (m_output)   { free_mem(m_output);   m_output   = nullptr; }
  }
  if (is_batch_changed || is_mem_size_changed || is_free_cn) {
    if (m_ctx) { xmrig::CnCtx::release(m_ctx, m_batch); delete [] m_ctx; m_ctx = nullptr; }
  }
  if (is_batch_changed || is_free_cn) {
    if (m_spads) { free_mem(m_spads); m_spads = nullptr; }
  }
  if (is_free_rx) {
    if (m_rx_dataset)     { randomx_release_dataset(m_rx_dataset); m_rx_dataset = nullptr; }
    if (m_rx_cache)       { randomx_release_cache(m_rx_cache); m_rx_cache = nullptr; }
    if (m_rx_dataset_mem) { delete m_rx_dataset_mem; m_rx_dataset_mem = nullptr; }
    if (m_rx_cache_mem)   { delete m_rx_cache_mem; m_rx_cache_mem = nullptr; }
  }
}

void Core::set_fn(cn_any_hash_fun fn) {
  m_fn.any     = fn;
  m_timestamp  = 0;
  m_hash_count = 0;
}

bool Core::process_message(const std::string& type, const MessageValues& v) {
  if (type == "test") {
    set_job(false, false, v);
    m_nonce  = 0;
    m_target = 0;

  } else if (type == "close") {
    if (m_nonce) send_last_nonce(m_nonce, m_pool_id);
    free_memory();
    return false; // stop processing messages
  }

  return true; // continue processing messages
}

void Core::Execute(const AsyncProgressQueueWorker<char>::ExecutionProgress& progress) {
  { // select best argon2 implementation
    const char* hint = nullptr;
#if defined(HAVE_SSSE2)
    if (ci.has(xmrig::ICpuInfo::FLAG_SSE2))    hint = "SSE2";
#endif
#if defined(HAVE_SSSE3)
    if (ci.has(xmrig::ICpuInfo::FLAG_SSSE3))   hint = "SSSE3";
#endif
#if defined(HAVE_XOP)
    if (ci.has(xmrig::ICpuInfo::FLAG_XOP))     hint = "XOP";
#endif
#if defined(HAVE_AVX2)
    if (ci.has(xmrig::ICpuInfo::FLAG_AVX2))    hint = "AVX2";
#endif
#if defined(HAVE_AVX512F)
    if      (ci.has(xmrig::ICpuInfo::FLAG_AVX512F)) hint = "AVX-512F";
#endif
    if (hint) argon2_select_impl_by_name(hint);
  }

  if (ci.arch() == xmrig::ICpuInfo::ARCH_ZEN)
    xmrig::RxFix::setupMainLoopExceptionFrame();

#if defined(__x86_64__) || defined(_M_X64)
  if (ci.has(xmrig::ICpuInfo::FLAG_SSE41)) rx_blake2b_compress = rx_blake2b_compress_sse41;
  if (ci.hasAVX2())                        rx_blake2b          = blake2b_avx2;
#endif

  randomx_set_scratchpad_prefetch_mode(0);
  randomx_set_huge_pages_jit(false);
  randomx_set_optimized_dataset_init(1);
  m_progress = &progress;

  while (true) {
    std::deque<Message> messages;
    fromNode.readAll(messages);
    for (const auto& message : messages) {
      try {
        if (!process_message(message.name, message.values)) return;
      } catch(const std::string& err) {
        send_error(std::string("Message processing exception: ") + err);
      }
    }


    // we skip first hash function run using m_hash_count check to exclude GPU compile time
    // that effectively skips it in test mode too
    static unsigned hashrate_check_counter = HASHRATE_COUNTER_INTERVAL;
    if (m_dev == DEV::RX_CPU) m_mutex_hashrate.lock();
    const unsigned hash_count = m_hash_count;
    if (m_dev == DEV::RX_CPU) m_mutex_hashrate.unlock();
    if (hash_count && --hashrate_check_counter == 0) {
      hashrate_check_counter = HASHRATE_COUNTER_INTERVAL;
      const uint64_t new_timestamp = std::chrono::time_point_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now()
      ).time_since_epoch().count();
      if (!m_timestamp || new_timestamp - m_timestamp > 60*1000) {
        if (m_timestamp) send_msg("hashrate", "hashrate", std::to_string(
          static_cast<float>(hash_count) / (new_timestamp - m_timestamp) * 1000.0f
        ));
        m_timestamp = new_timestamp;
        if (m_dev == DEV::RX_CPU) m_mutex_hashrate.lock();
        m_hash_count = 0;
        if (m_dev == DEV::RX_CPU) m_mutex_hashrate.unlock();
      }
    }

    if (m_fn.any) {
      try {
        switch (m_dev) {
          case DEV::CPU:
            m_fn.cpu(m_input_cn, m_input_cn_len, m_output, m_ctx, m_height);
            break;

          case DEV::RX_CPU: throw "Internal error: Unreachable code executed";
        }
      } catch(const std::string& err) {
        send_error(std::string("Compute function exception: ") + err);
        set_fn(nullptr);
        continue;
      } catch(...) {
        send_error("Compute function exception");
        set_fn(nullptr);
        continue;
      }

      if (!m_nonce) { // test job
        std::string result_hash_str;
        for (unsigned i = 0; i != m_batch; ++ i) {
          if (i) result_hash_str += " ";
          char hash[HASH_LEN*2+1];
          result_hash_str += hash_bin2hex(hash, i);
        }
        send_msg("test", "result", result_hash_str);
        set_fn(nullptr);
        continue;
      }

      m_hash_count += m_batch; // here we do not need mutex since there are no threads

      const uint32_t prev_nonce = m_nonce;
      for (unsigned i = 0; i != m_batch; ++i) {
        uint32_t* const pnonce = get_nonce_cn(i);
        if (m_target && *get_result(i) < m_target) send_result(*pnonce, m_output + HASH_LEN * i);
        *pnonce = m_nonce;
        m_nonce += m_nonce_step;
      }

      if (m_target && ( m_is_nicehash ? (prev_nonce & 0xFF000000) != (m_nonce & 0xFF000000) :
                        prev_nonce > m_nonce )
      ) {
        send_error("Nonce overflow");
        set_fn(nullptr);
        continue;
      }

    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }
}

AsyncWorker* create_worker(
  Nan::Callback* const data, Nan::Callback* const complete, Nan::Callback* const error_callback,
  v8::Local<v8::Object>& options
) {
  return new Core(data, complete, error_callback, options);
}

NODE_MODULE(moner_core, AsyncWorkerWrapper::Init)
