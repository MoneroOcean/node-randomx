// Copyright GNU GPLv3 (c) 2023-2025 MoneroOcean <support@moneroocean.stream>

"use strict";

const path    = require("path");
const randomx = require(path.join(__dirname, "..", "index.js"));

// compute core wrapper for cluster process fork
if (randomx.cluster_process()) return;

const args = process.argv.slice(2);
const job          = JSON.parse(args.shift());
const result_hexes = args;
const seenRxThreads = new Set();

function exit(code) {
  randomx.messageWorkers({type: "close"});
  process.exitCode = code;
  return false;
}

// handles messages sent to the master thread from worker threads
function messageHandler(msg) {
  switch (msg.type) {
    case "test": {
      const is_rx = job.algo.includes("rx/");
      const rxThreadId = Number(msg.value.rx_thread_id);
      if (is_rx && (!Number.isInteger(rxThreadId) || rxThreadId < 0 ||
          rxThreadId >= result_hexes.length || seenRxThreads.has(rxThreadId))) {
        console.error(`FAILED: Invalid or duplicate RX batch slot ${msg.value.rx_thread_id}`);
        return exit(1);
      }
      // duplicate test result for batch size
      const result_hex2 = is_rx ? result_hexes[rxThreadId] : result_hexes[0];
      // for rx algos threads are encoded in batch
      const batch = randomx.get_dev_batch(randomx.get_thread_dev(msg.thread_id, job.dev));
      const rx_batch = is_rx ? 1 : batch;
      let result_hex3 = result_hex2;
      for (let i = 1; i < rx_batch; ++ i) result_hex3 += ` ${result_hex2}`;
      if (msg.value.result !== result_hex3) {
        console.error(`FAILED: ${msg.value.result} != ${result_hex3}`);
        return exit(1);
      }
      if (is_rx) {
        seenRxThreads.add(rxThreadId);
        if (seenRxThreads.size !== result_hexes.length) return true;
      }
      console.log("PASSED");
      return exit(0);
    }

    case "error":
      console.error(`Compute core error: ${JSON.stringify(msg.value)}`);
      return exit(1); // exit with error

    default:
      console.error(`Unknown master thread message: ${JSON.stringify(msg)}`);
  }
}
randomx.create_thread(messageHandler);
randomx.messageWorkers({type: "test", job});
