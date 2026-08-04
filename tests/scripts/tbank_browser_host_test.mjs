// SPDX-License-Identifier: MIT

import assert from 'node:assert/strict'
import {
  extractTBankSessionId,
  findChromiumExecutable
} from '../../scripts/tbank_browser_host.mjs'

const session = '0123456789abcdef0123456789abcdef.browser-session'

assert.equal(extractTBankSessionId(
  `https://www.tbank.ru/api/common/v1/session_status?sessionid=${session}`),
session)
assert.equal(extractTBankSessionId(
  `https://www.tbank.ru:443/api/common/v1/ping?origin=web&sessionid=${session}`),
session)

for (const rejected of [
  `http://www.tbank.ru/api/common/v1/ping?sessionid=${session}`,
  `https://tbank.ru/api/common/v1/ping?sessionid=${session}`,
  `https://www.tbank.ru.evil.example/api/common/v1/ping?sessionid=${session}`,
  `https://www.tbank.ru/payments?sessionid=${session}`,
  `https://www.tbank.ru/api/common/v1/ping?sessionid=${session}&sessionid=${session}`,
  'https://www.tbank.ru/api/common/v1/ping?sessionid=too-short',
  'https://www.tbank.ru/api/common/v1/ping?sessionid=bad%3Bsession',
  'not-a-url'
]) {
  assert.equal(extractTBankSessionId(rejected), null)
}

assert.equal(typeof findChromiumExecutable() === 'string' ||
             findChromiumExecutable() === null, true)

process.stdout.write('T-Bank browser host tests passed\n')
