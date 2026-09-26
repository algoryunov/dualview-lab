import os from 'node:os'
import path from 'node:path'
import assert from 'node:assert/strict'
import { chromium } from '@playwright/test'
const browser = await chromium.launch({
  executablePath: process.env.CHROME_PATH || undefined,
  headless: true,
  args: [
    '--use-fake-device-for-media-stream',
    '--use-fake-ui-for-media-stream',
    '--autoplay-policy=no-user-gesture-required',
  ],
})
const origin = process.env.TEST_ORIGIN || 'http://localhost:18445'
const context = await browser.newContext({ permissions: ['camera'], ignoreHTTPSErrors: true })
const page = await context.newPage()
const errors = []
const answers = []
function observeSignaling(tab) {
  tab.on('websocket', (socket) =>
    socket.on('framereceived', ({ payload }) => {
      const message = JSON.parse(String(payload))
      if (message.type === 'answer') answers.push(message.sdp)
    }),
  )
}
observeSignaling(page)
page.on('pageerror', (e) => errors.push(String(e)))
page.on('console', (m) => {
  if (m.type() === 'error') console.log('BROWSER', m.text())
})
try {
  await page.goto(origin)
  await page.getByText('Local connection', { exact: false }).waitFor({ timeout: 30000 })
  await page.getByText('Metal log: Recording', { exact: true }).waitFor({ timeout: 15000 })
  await page.getByRole('button', { name: 'Settings', exact: true }).click()
  await page.getByRole('button', { name: 'Connect phone', exact: true }).click()
  await page.getByRole('link').filter({ hasText: '/phone?session=' }).waitFor()
  assert.equal(
    await page.getByAltText('Phone pairing QR code').count(),
    process.env.EXPECT_QR === 'false' ? 0 : 1,
  )
  const url = await page.locator('.pairing-link').getAttribute('href')
  await page.getByRole('button', { name: 'Close pairing' }).click()
  let phone = await context.newPage()
  observeSignaling(phone)
  phone.on('pageerror', (e) => errors.push(String(e)))
  await phone.goto(url)
  await phone.getByRole('button', { name: 'Start camera', exact: true }).click()
  await phone
    .getByText('Camera connected. Video is processed on the local server.', { exact: true })
    .waitFor({ timeout: 30000 })
  // A second phone replaces the active sender without erasing its new frames.
  const replacedPhone = phone
  phone = await context.newPage()
  observeSignaling(phone)
  phone.on('pageerror', (e) => errors.push(String(e)))
  await phone.goto(url)
  if (process.env.EXPECT_QR === 'false') {
    await phone.getByLabel('Capture quality', { exact: true }).selectOption('1080')
    await phone.reload()
    assert.equal(await phone.getByLabel('Capture quality', { exact: true }).inputValue(), '1080')
  }
  await phone.getByRole('button', { name: 'Start camera', exact: true }).click()
  await phone
    .getByText('Camera connected. Video is processed on the local server.', { exact: true })
    .waitFor({ timeout: 30000 })
  await replacedPhone.getByRole('button', { name: 'Start camera', exact: true }).waitFor()
  await page.getByRole('button', { name: 'Settings', exact: true }).click()
  await page.waitForFunction(
    () => {
      const video = document.querySelectorAll('video')[1]
      if (!video || video.videoWidth < 1) return false
      const canvas = document.createElement('canvas')
      canvas.width = 32
      canvas.height = 32
      const ctx = canvas.getContext('2d')
      ctx.drawImage(video, 0, 0, 32, 32)
      return ctx
        .getImageData(0, 0, 32, 32)
        .data.some((value, index) => index % 4 !== 3 && value > 30)
    },
    null,
    { timeout: 30000 },
  )
  const captured = await phone
    .locator('video')
    .evaluate((video) => [video.videoWidth, video.videoHeight])
  assert.ok(captured[0] >= 1280 && captured[1] >= 720, 'Synthetic phone capture must be HD')
  assert.ok(
    answers.length >= 3 &&
      answers.every(
        (sdp) => /a=rtpmap:\d+ rtx\/90000/i.test(sdp) && /a=rtcp-fb:\d+ nack\r?\n/.test(sdp),
      ),
    'All peers must negotiate retransmission and packet-loss feedback',
  )
  await phone
    .getByText(`Sending: ${captured[0]} × ${captured[1]}`, { exact: false })
    .waitFor({ timeout: 30000 })
  await page.waitForFunction(
    ([width, height]) => {
      const preview = document.querySelectorAll('video')[1]
      return preview?.videoWidth === width && preview?.videoHeight === height
    },
    captured,
    { timeout: 30000 },
  )
  await page.getByText('Diagnostics and interaction', { exact: true }).click()
  const diagnostics = JSON.parse(await page.locator('pre').innerText())
  assert.equal(diagnostics.processing.error, undefined)
  if (process.env.EXPECT_PROVIDER)
    assert.equal(diagnostics.processing.provider, process.env.EXPECT_PROVIDER)
  await page.getByRole('button', { name: 'Reset object', exact: true }).click()
  assert.equal(await page.getByRole('alert').count(), 0)
  await page.getByRole('button', { name: 'Other Modes', exact: true }).click()
  for (const name of ['3D Models', 'Light Trails', 'Energy']) {
    await page.getByRole('button', { name, exact: true }).click()
    await page.waitForTimeout(400)
  }
  await phone.getByRole('button', { name: 'Stop camera', exact: true }).click()
  await phone.getByRole('button', { name: 'Start camera', exact: true }).click()
  await phone
    .getByText('Camera connected. Video is processed on the local server.', { exact: true })
    .waitFor({ timeout: 30000 })
  await page.reload()
  await page.getByText('Local connection', { exact: false }).waitFor({ timeout: 30000 })
  await page.getByRole('button', { name: 'Liquid Metal', exact: true }).click()
  await page.getByRole('button', { name: 'Auto demo', exact: true }).click()
  await page.getByText('Auto demo is active.', { exact: false }).waitFor()
  await page.screenshot({ path: path.join(os.tmpdir(), 'dualview-desktop.png') })
  await page.setViewportSize({ width: 390, height: 844 })
  await page.screenshot({ path: path.join(os.tmpdir(), 'dualview-mobile.png') })
  await page.route('**/api/session', async (route) => {
    const response = await route.fetch()
    await route.fulfill({ response, json: { ...(await response.json()), expires_in_seconds: 1 } })
  })
  await page.getByRole('button', { name: 'Settings', exact: true }).click()
  await page.getByRole('button', { name: 'Connect phone', exact: true }).click()
  await page.getByText('This connection link has expired.', { exact: false }).waitFor()
  assert.equal(await page.locator('.pairing-link').count(), 0)
  assert.equal(await page.getByAltText('Phone pairing QR code').count(), 0)
  await page.getByRole('button', { name: 'Close pairing' }).click()
  if (errors.length) throw new Error(errors.join('\n'))
  console.log('PASS: native WebRTC data channel, phone video receive/forward, tabs, and rendering')
} catch (error) {
  console.log(await page.locator('body').innerText())
  await page.screenshot({ path: path.join(os.tmpdir(), 'dualview-failure.png') })
  throw error
} finally {
  await browser.close()
}
