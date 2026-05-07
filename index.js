// Copyright GNU GPLv3 (c) 2023-2025 MoneroOcean <support@moneroocean.stream>

"use strict";

const path    = require("path");
const events  = require("events");
const cluster = require("cluster");
const fs      = require('fs');

const thread_id = cluster.isMaster ? "master" : parseInt(process.env["thread_id"]);
let worker_ids = []; // active worker ids (cluster.workers can contain not yet closed workers)

module.exports.create_core = function() {
  const deploy_path = path.join(__dirname, "./node-randomx.node");
  const core_path   = fs.existsSync(deploy_path) ? deploy_path :
                      path.join(__dirname, "/build/Release/node-randomx.node");
  const core_module = require(core_path);
  let emitter = new events();
  let worker = new core_module.AsyncWorker(
    function(name, value) {
      emitter.emit(name, value);
    },
    function ()     { emitter.emit("close"); },
    function(error) { emitter.emit("error", error); },
    {} // no extra options
  );
  return {
    from:    emitter,
    emit_to: function(name, data) {
      worker.sendToCpp(name, data ? data : {});
    }
  };
};

module.exports.cluster_process = function() {
  if (cluster.isMaster) return false;

  // process worker thread env vars
  let compute_core = this.create_core();

  // send message from worker thread to master thread
  function send_msg(type, value) {
    return process.send({type: type, value: value, thread_id: thread_id});
  }
  compute_core.from.on("test",   function(v) { send_msg("test", v); });
  compute_core.from.on("result", function(v) { send_msg("result", v); });
  compute_core.from.on("error",  function(v) { send_msg("error", v); });
  compute_core.from.on("close",  function()  { process.exit(0); });

  // process messages from the master thread
  process.on("message", function(msg) {
    switch (msg.type) {
      case "job": case "bench": case "test":
        // find dev for this specific thread from msg.job.dev list
        msg.job.thread_id = thread_id;
        compute_core.emit_to(msg.type, msg.job);
        break;
      case "pause": case "close":
        compute_core.emit_to(msg.type);
        break;
      default: console.error("Unknown thread message");
    }
  });

  return true;
};

module.exports.messageWorkers = function(msg) {
  for (const worker_id of worker_ids) if (cluster.workers[worker_id]) cluster.workers[worker_id].send(msg);
};

module.exports.record_thread = function(thread_id) {
  worker_ids = []
  worker_ids.push(thread_id);
};

module.exports.is_compute_thread = function(thread_id) {
  return worker_ids.includes(thread_id);
};

module.exports.create_thread = function(messageHandler) {
  let thread = cluster.fork({thread_id: 0});
  thread.on("message", messageHandler);
  module.exports.record_thread(thread.id);
};

// get thread dev stripping ^thread specification from it
module.exports.get_thread_dev = function(thread_id, devs) {
  const dev_parts = devs.split(",");
  let thread_count = 0;
  for (const dev_part of dev_parts) {
    const m = dev_part.match(/^([^\^]+)\^(\d+)$/);
    thread_count += m ? parseInt(m[2]) : 1;
    if (thread_id < thread_count) return m ? m[1] : dev_part;
  }
  console.error("Can't find " + thread_id + " thread device in " + devs + " specification");
  return null;
};

// return dev *batch value
module.exports.get_dev_batch = function(dev) {
  const m = dev.match(/\*(\d+)$/);
  return m ? parseInt(m[1]) : 1;
};
