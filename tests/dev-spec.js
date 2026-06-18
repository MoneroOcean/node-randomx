// Copyright GNU GPLv3 (c) 2023-2025 MoneroOcean <support@moneroocean.stream>

"use strict";

// Pure-JS unit tests for the device-spec helpers in index.js. These do not need
// the native addon, so they can run even when the .node binary is absent.

const assert = require("node:assert/strict");
const nodeTest = require("node:test");
const randomx = require("../index.js");

nodeTest("get_thread_dev resolves devices within range", () => {
  assert.equal(randomx.get_thread_dev(0, "cpu"), "cpu");
  assert.equal(randomx.get_thread_dev(0, "cpu^2,gpu"), "cpu");
  assert.equal(randomx.get_thread_dev(1, "cpu^2,gpu"), "cpu");
  assert.equal(randomx.get_thread_dev(2, "cpu^2,gpu"), "gpu");
});

nodeTest("get_thread_dev returns null when the spec does not cover the thread", () => {
  assert.equal(randomx.get_thread_dev(5, "cpu^2,gpu"), null);
});

nodeTest("get_dev_batch parses the *batch suffix", () => {
  assert.equal(randomx.get_dev_batch("cpu"), 1);
  assert.equal(randomx.get_dev_batch("cpu*2"), 2);
  assert.equal(randomx.get_dev_batch("gpu*16"), 16);
});

nodeTest("get_dev_batch degrades to the default batch on a null device", () => {
  // Canonical consumer pattern get_dev_batch(get_thread_dev(...)) must not throw
  // a TypeError when get_thread_dev returns null for an out-of-range thread.
  assert.equal(randomx.get_dev_batch(null), 1);
  assert.equal(randomx.get_dev_batch(undefined), 1);
  assert.equal(
    randomx.get_dev_batch(randomx.get_thread_dev(5, "cpu^2,gpu")),
    1,
  );
});
