// SPDX-License-Identifier: MIT

'use strict'

global.main = async function main () {
  await fetch('https://example.com/private')
  ZenMoney.setResult({ success: true })
}
