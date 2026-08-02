// SPDX-License-Identifier: MIT

'use strict'

global.main = async function main () {
  await fetch('https://www.tbank.ru/api/common/v1/ping', { method: 'POST' })
  ZenMoney.setResult({ success: true })
}
