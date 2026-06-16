<div align="center">

# node-randomx

Native Node.js addon for fast RandomX and CryptoNight family PoW hashing, built on XMRig

<p>
  <a href="package.json"><img src="https://img.shields.io/badge/license-GPL--3.0--or--later-blue.svg" alt="License"></a>
  <img src="https://img.shields.io/badge/node-%E2%89%A522.9.0-brightgreen.svg" alt="Node >=22.9.0">
  <img src="https://img.shields.io/badge/platform-Linux%20%7C%20macOS-lightgrey.svg" alt="Platform">
  <img src="https://img.shields.io/badge/native-C%2B%2B%20addon-orange.svg" alt="Native C++ addon">
  <img src="https://img.shields.io/badge/focus-RandomX%20hashing-c2410c.svg" alt="Focus: RandomX hashing">
  <a href="https://github.com/MoneroOcean"><img src="https://img.shields.io/badge/MoneroOcean-ecosystem-6f42c1.svg" alt="MoneroOcean"></a>
</p>

</div>

## Overview

node-randomx is a native C++ addon that exposes CPU proof-of-work hashing to Node.js. It wraps the
hashing cores from [XMRig](https://github.com/xmrig/xmrig) (vendored under `xmrig/`) and drives them
through an asynchronous, message-based worker so that hashing runs off the main event loop.

The addon is built around the RandomX algorithm family but also covers the broader CryptoNight,
Argon2 and GhostRider algorithms that XMRig supports. It is used in the MoneroOcean stack as the
RandomX hashing backend; for the multi-algo addon see the sibling
[node-powhash](https://github.com/MoneroOcean/node-powhash).

## Features

- Asynchronous native worker: hashing runs on a background thread pool (`ctpl-stl.h`) and reports
  back via an `EventEmitter`-style message bus, keeping the Node.js event loop responsive.
- Cluster-friendly: a thin `cluster` helper layer forks one compute process per logical thread and
  routes job/result messages between the master and workers, with automatic logical thread-id
  allocation.
- Wide algorithm coverage via XMRig cores, exercised by the test suite, including:
  - RandomX: `rx/0`, `rx/2`, `rx/wow`, `rx/arq`, `rx/graft`, `rx/sfx`, `rx/yada`
  - CryptoNight: `cn/0`, `cn/1`, `cn/2`, `cn/r`, `cn/fast`, `cn/half`, `cn/xao`, `cn/rto`,
    `cn/rwz`, `cn/zls`, `cn/double`, `cn/ccx`, `cn/upx2`
  - CryptoNight variants: `cn-lite/0`, `cn-lite/1`, `cn-heavy/0`, `cn-heavy/xhv`, `cn-heavy/tube`,
    `cn-pico/0`, `cn-pico/tlo`
  - Argon2: `argon2/chukwa`, `argon2/chukwav2`, `argon2/wrkz`
  - GhostRider: `ghostrider`
- Device/batch specification: jobs select a device with a `dev` string such as `cpu`, `cpu*2`
  (batch size) or `cpu^4` (thread count), and the helpers parse these specs.
- Build-time CPU feature detection: `test-cpu.sh` probes the host (SSE2/SSSE3/SSE4.1/XOP/AVX2/
  AVX-512/VAES/AES/MSR, plus x86_64 vs ARM) and `binding.gyp` compiles only the applicable XMRig
  sources and assembly.

## Architecture

| Layer | File(s) | Role |
| --- | --- | --- |
| JS API / cluster glue | `index.js` | `create_core`, cluster fork helpers, device/batch parsing |
| Async worker base | `async-worker.h` | Nan `AsyncProgressQueueWorker` plumbing for C++ ↔ JS messages |
| Hashing core | `core.h`, `core.cpp` | Algorithm selection, memory/VM setup, hashing loop |
| Job handling | `job.cpp` | Parses job messages, runs the per-thread hashing loop, emits results |
| Thread pool | `ctpl-stl.h` | C++ thread pool used to spread RandomX work across threads |
| Vendored hashing | `xmrig/` | XMRig crypto cores (RandomX, CryptoNight, Argon2, GhostRider) |
| Native build | `binding.gyp`, `test-cpu.sh` | gyp build with host CPU feature detection |

Internally the native worker is created with three JS callbacks (data, complete, error). Messages
are sent into the core with `emit_to(name, data)` and received back through the emitter. The worker
exits and emits `close` when a `close` message is processed.

## Install

Requirements:

- Node.js `>=22.9.0` and npm `>=11.10.0` (enforced via `engines` and `engine-strict=true`).
- A C++ toolchain and `node-gyp` build prerequisites (compiler, make, Python) — the addon is built
  from source against the vendored XMRig sources.
- Currently targets Linux and macOS hosts (x86_64 and ARM64/ARMv7); `test-cpu.sh` and `binding.gyp`
  branch on `uname`.

```sh
npm install
```

This builds the native addon into `build/Release/node-randomx.node`. At runtime `index.js` prefers a
deployed `node-randomx.node` next to the module if present, otherwise falls back to the
`build/Release` path.

The only runtime dependency is [`nan`](https://www.npmjs.com/package/nan). Note `.npmrc` sets
`min-release-age=7`, so very recently published dependency versions are held back.

## Usage

Create a compute core and exchange messages with it:

```js
const randomx = require("node-randomx");

const core = randomx.create_core();

core.from.on("result", (v) => console.log("share found", v));
core.from.on("test",   (v) => console.log("test hash", v));
core.from.on("error",  (e) => console.error("core error", e));
core.from.on("close",  () => console.log("core closed"));

// run a one-shot test hash
core.emit_to("test", {
  dev:      "cpu",
  algo:     "rx/0",
  blob_hex: "5468697320697320612074657374",      // input blob, hex
  seed_hex: "3132333435363738393031323334353637383930313233343536373839303132", // RandomX seed
});

// when done
core.emit_to("close");
```

### Cluster helpers

For multi-process hashing, `index.js` provides a cluster layer:

- `randomx.cluster_process()` — call early in a worker process; returns `true` if running as a
  forked compute worker (it wires the core to the master and returns), `false` in the master.
- `randomx.create_thread(messageHandler)` — fork a compute worker, assign it a logical thread id and
  attach a message handler. Returns the cluster worker.
- `randomx.messageWorkers(msg)` — broadcast a message to all active compute workers.
- `randomx.is_compute_thread(id)` / `randomx.record_thread(id)` — track active worker ids.
- `randomx.get_thread_dev(thread_id, devs)` / `randomx.get_dev_batch(dev)` — parse `dev`
  specifications such as `cpu^4` (4 threads) and `cpu*2` (batch size 2).

See `tests/worker.js` and `tests/thread-ids.js` for complete master/worker examples.

## Job message keys

Jobs are plain objects sent with `emit_to("test" | "job" | "bench", job)`. Keys parsed by the core
(`job.cpp`):

| Key | Required | Description |
| --- | --- | --- |
| `dev` | yes | Device spec, e.g. `cpu`, `cpu*2` (batch), `cpu^4` (threads) |
| `algo` | yes | Algorithm name (see Features) |
| `blob_hex` | yes | Input blob, hex-encoded |
| `seed_hex` | for RandomX | RandomX seed hash, hex-encoded |
| `job_id` | no | Opaque id echoed back on results |
| `height` | no | Block height (used by height-dependent algos such as `cn/r`) |
| `nonce` | no | Starting nonce |
| `thread_id` | no | Logical thread id (set automatically by cluster helpers) |
| `thread_num` | no | Number of threads |
| `is_nicehash` | no | Restrict nonce to the NiceHash range |

Result messages carry the computed `result` hash (and `job_id` when supplied).

## Testing

```sh
npm test
```

The test runner uses the Node.js built-in test runner (`node --test`, single-concurrency) via
`tests/all.js`. It will build the native addon first if it is missing (`npm rebuild`, or
`npm install` when dependencies are absent), then runs a known-answer vector for each supported
algorithm plus a cluster thread-id test.

Caveats:

- Tests require the native addon to compile, so a working C++ toolchain is needed; the first run can
  be slow while the addon builds.
- Tests fork real compute processes and run live hashing, so they are CPU-intensive (each algorithm
  case has a generous timeout).
- The built sources depend on host CPU features and platform, so results are validated against the
  actual machine the suite runs on.

## MoneroOcean ecosystem

| Component | Role |
| --- | --- |
| [nodejs-pool](https://github.com/MoneroOcean/nodejs-pool) | Pool backend — stratum, share storage, payments |
| [mo-pool-ui](https://github.com/MoneroOcean/mo-pool-ui) | Static web frontend for the pool |
| [xmr-node-proxy](https://github.com/MoneroOcean/xmr-node-proxy) | Stratum proxy / share aggregator |
| [mo-miner](https://github.com/MoneroOcean/mo-miner) | MoneroOcean end-user CPU/GPU mining client (multi-algo) |
| [multi-miner](https://github.com/MoneroOcean/multi-miner) | Multi-algo miner manager |
| [node-powhash](https://github.com/MoneroOcean/node-powhash) | Native multi-algo PoW hashing addon |
| [node-randomx](https://github.com/MoneroOcean/node-randomx) | Native RandomX hashing addon |
| [node-blocktemplate](https://github.com/MoneroOcean/node-blocktemplate) | Native block-template & serialization addon |
| [grpc-json-proxy](https://github.com/MoneroOcean/grpc-json-proxy) | gRPC ↔ JSON-RPC proxy (Tari base node) |

## License

GPL-3.0-or-later. The vendored `xmrig/` sources are licensed under the GNU GPLv3 by their
respective authors.
