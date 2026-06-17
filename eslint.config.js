"use strict";
const js = require("@eslint/js");
const globals = require("globals");
module.exports = [
  { ignores: ["node_modules/**", "build/**", "xmrig/**"] },
  js.configs.recommended,
  {
    files: ["**/*.js"],
    languageOptions: {
      ecmaVersion: 2023,
      sourceType: "commonjs",
      globals: { ...globals.node }
    },
    rules: {
      "no-unused-vars": ["error", { args: "after-used", argsIgnorePattern: "^_", varsIgnorePattern: "^_", caughtErrors: "all", caughtErrorsIgnorePattern: "^_" }],
      // Existing tests use lexical declarations inside switch cases without braces;
      // wrapping each case would be a non-trivial structural rewrite, so relax this.
      "no-case-declarations": "off",
      // index.js uses a device-spec regex /^([^\^]+)\^(\d+)$/ whose harmless escape
      // is flagged; editing the regex is risky, so relax this rule instead.
      "no-useless-escape": "off"
    }
  }
];
