// Copyright GNU GPLv3 (c) 2023-2025 MoneroOcean <support@moneroocean.stream>

"use strict";

const assert = require("node:assert/strict");
const cluster = require("node:cluster");
const randomx = require("../index.js");

if (!cluster.isPrimary) {
  process.send({
    type: "thread_id",
    thread_id: Number(process.env.thread_id),
  });

  process.on("message", function(msg) {
    if (msg.type === "close") process.exit(0);
  });
} else {
  const workers = [];
  const thread_ids = [];
  const exit_seen_compute = new Set();
  let finished = false;

  function exit(code) {
    if (finished) return;
    finished = true;
    clearTimeout(timeout);

    for (const worker of workers) {
      if (!worker.isDead()) worker.kill();
    }

    process.exit(code);
  }

  function fail(err) {
    console.error(err && err.stack ? err.stack : err);
    exit(1);
  }

  function waitForExit(worker) {
    return new Promise(function(resolve) {
      if (worker.isDead()) resolve();
      else worker.once("exit", resolve);
    });
  }

  async function maybeDone() {
    if (thread_ids.length !== 2) return;

    try {
      assert.deepEqual(thread_ids.toSorted(), [0, 1]);
      for (const worker of workers) {
        assert.equal(randomx.is_compute_thread(worker.id), true);
      }

      function onClusterExit(worker) {
        if (randomx.is_compute_thread(worker.id)) exit_seen_compute.add(worker.id);
      }
      cluster.on("exit", onClusterExit);

      randomx.messageWorkers({type: "close"});
      await Promise.all(workers.map(waitForExit));
      cluster.off("exit", onClusterExit);
      await new Promise(function(resolve) { setImmediate(resolve); });

      for (const worker of workers) {
        assert.equal(exit_seen_compute.has(worker.id), true);
      }

      for (const worker of workers) {
        assert.equal(randomx.is_compute_thread(worker.id), false);
      }

      exit(0);
    } catch (err) {
      fail(err);
    }
  }

  function messageHandler(msg) {
    if (msg.type !== "thread_id") return;
    thread_ids.push(msg.thread_id);
    maybeDone();
  }

  const timeout = setTimeout(function() {
    fail(new Error("Timed out waiting for worker thread ids"));
  }, 30000);

  workers.push(randomx.create_thread(messageHandler));
  workers.push(randomx.create_thread(messageHandler));
}
