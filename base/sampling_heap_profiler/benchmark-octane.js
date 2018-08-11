// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// To benchmark a specific version of Chrome set the CHROME_PATH environment
// variable, e.g.:
// $ CHROME_PATH=~/chromium/out/Release/chrome node benchmark-octane.js

const puppeteer = require('puppeteer');

let base_score;

async function runOctane(samplingRate) {
  const args = ['--enable-devtools-experiments'];
  if (samplingRate)
    args.push(`--sampling-heap-profiler=${samplingRate}`);
  const browser = await puppeteer.launch({
      executablePath: process.env.CHROME_PATH, args, headless: true});
  try {
    const page = await browser.newPage();
    await page.goto('https://chromium.github.io/octane/');
    await page.waitForSelector('#run-octane');  // Just in case.
    await page.click('#run-octane');

    const scoreDiv = await page.waitForSelector('#main-banner:only-child',
        {timeout: 120000});
    const scoreText = await page.evaluate(e => e.innerText, scoreDiv);
    const match = /Score:\s*(\d+)/.exec(scoreText);
    if (match.length < 2) {
      console.log(`Error: cannot parse score from '${scoreText}'`);
      return 0;
    }
    return parseInt(match[1]);
  } finally {
    await browser.close();
  }
}

async function makeRuns(rate) {
  console.log(`tesing rate: ${rate}`);
  let sum = 0;
  let sum2 = 0;
  const n = 10;
  for (let i = 0; i < n; ++i) {
    const score = await runOctane(rate);
    console.log(score);
    sum += score;
    sum2 += score * score;
  }
  const mean = sum / n;
  const stdev = Math.sqrt(sum2 / n - mean * mean);
  console.log(`rate: ${rate}   mean: ${mean}   stdev: ${stdev}`);
  return mean;
}

async function main() {
  console.log(`Using ${process.env.CHROME_PATH || puppeteer.executablePath()}`);
  const base_score = await makeRuns(0);
  for (let rate = 32; rate <= 2048; rate *= 2) {
    const score = await makeRuns(rate);
    console.log(`slowdown: ${(100 - score / base_score * 100).toFixed(2)}%\n`);
  }
}

main();

