// SPDX-License-Identifier: MIT

'use strict'

const assert = require('node:assert/strict')
const path = require('node:path')

const bundlePath = path.resolve(
  __dirname, '../../bundles/tbank-retail/tbank-retail.cjs'
)

async function importsAccountsAndOperations () {
  const requests = []
  global.fetch = async input => {
    const url = new URL(String(input))
    requests.push(url)
    assert.equal(url.origin, 'https://www.tbank.ru')
    assert.equal(url.searchParams.get('sessionid'), 'verified-session-123456')

    let payload
    if (url.pathname.endsWith('/ping')) {
      payload = { accessLevel: 'CLIENT' }
    } else if (url.pathname.endsWith('/accounts_light_ib')) {
      payload = [{
        id: 'account-1',
        name: 'Black',
        accountType: 'Debit',
        moneyAmount: {
          value: 12500.25,
          currency: { name: 'RUR' }
        },
        cards: [{ value: '553691******1234' }]
      }]
    } else if (url.pathname.endsWith('/operations')) {
      assert.equal(url.searchParams.get('account'), 'account-1')
      assert.equal(url.searchParams.get('start'), '1782864000000')
      assert.equal(url.searchParams.get('end'), '1785542400000')
      payload = [{
        id: 'operation-1',
        status: 'OK',
        type: 'Debit',
        accountAmount: {
          value: -427.5,
          currency: { name: 'RUB' }
        },
        operationTime: { milliseconds: 1785489300000 },
        debitingTime: { milliseconds: 1785489400000 },
        merchant: { name: 'Кофейня' },
        spendingCategory: { name: 'Кафе' },
        mcc: 5814
      }]
    } else {
      assert.fail(`Unexpected endpoint: ${url.pathname}`)
    }
    return new Response(JSON.stringify({ resultCode: 'OK', payload }), {
      status: 200,
      headers: { 'Content-Type': 'application/json' }
    })
  }

  const accounts = []
  const transactions = []
  let result
  global.ZenMoney = {
    getData: (key, fallback) => key === 'psid'
      ? 'verified-session-123456'
      : fallback,
    addAccount: records => accounts.push(...records),
    addTransaction: records => transactions.push(...records),
    setResult: value => { result = value }
  }

  const bundle = require(bundlePath)
  assert.equal(bundle.convertAccount({
    id: 'credit-1',
    name: 'Platinum',
    accountType: 'Credit',
    moneyAmount: { value: 55000, currency: { name: 'RUB' } },
    creditLimit: { value: 100000, currency: { name: 'RUB' } }
  }).balance, -45000)
  await bundle.main({
    fromDate: new Date('2026-07-01T00:00:00.000Z'),
    toDate: new Date('2026-08-01T00:00:00.000Z')
  })

  assert.deepEqual(result, { success: true })
  assert.equal(requests.length, 3)
  assert.deepEqual(accounts, [{
    id: 'tbank:account-1',
    title: 'Black',
    type: 'checking',
    instrument: 'RUB',
    balance: 12500.25,
    syncID: ['account-1', '553691******1234']
  }])
  assert.equal(transactions.length, 1)
  assert.equal(transactions[0].id, 'tbank:operation-1')
  assert.equal(transactions[0].outcome, 427.5)
  assert.equal(transactions[0].outcomeAccount, 'tbank:account-1')
  assert.equal(transactions[0].comment, 'Кофейня')
  assert.equal(transactions[0].category, 'Кафе')
  assert.equal(transactions[0].mcc, 5814)
  assert.equal(transactions[0].hold, false)
}

async function rejectsExpiredSessions () {
  global.fetch = async () => new Response(JSON.stringify({
    resultCode: 'INSUFFICIENT_PRIVILEGES',
    errorMessage: 'Authentication required'
  }), {
    status: 200,
    headers: { 'Content-Type': 'application/json' }
  })
  global.ZenMoney = {
    getData: () => 'expired-session-123456',
    addAccount: () => assert.fail('Expired session imported an account'),
    addTransaction: () => assert.fail('Expired session imported a transaction'),
    setResult: () => assert.fail('Expired session reported success')
  }

  const { main } = require(bundlePath)
  await assert.rejects(
    main({
      fromDate: new Date('2026-07-01T00:00:00.000Z'),
      toDate: new Date('2026-08-01T00:00:00.000Z')
    }),
    /Сессия Т-Банка истекла/
  )
}

importsAccountsAndOperations()
  .then(rejectsExpiredSessions)
  .catch(error => {
    process.stderr.write(`${error.stack ?? error}\n`)
    process.exitCode = 1
  })
