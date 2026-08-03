// SPDX-License-Identifier: MIT

'use strict'

const { createHash } = require('node:crypto')

const origin = 'https://www.tbank.ru'
const apiPrefix = '/api/common/v1/'

function text (value, fallback = '') {
  const normalized = String(value ?? '').replace(/[\r\n\t]+/g, ' ').trim()
  return (normalized || fallback).slice(0, 256)
}

function number (value) {
  const result = typeof value === 'number' ? value : Number(value)
  return Number.isFinite(result) ? result : null
}

function amount (...values) {
  for (const value of values) {
    const result = number(value?.value ?? value)
    if (result !== null) return result
  }
  return null
}

function currency (...values) {
  for (const value of values) {
    const candidate = value?.currency?.name ?? value?.currency?.strCode ??
      value?.strCode ?? value?.name ?? value
    if (typeof candidate !== 'string') continue
    const result = candidate.trim().toUpperCase() === 'RUR'
      ? 'RUB'
      : candidate.trim().toUpperCase()
    if (/^[A-Z]{3}$/.test(result)) return result
  }
  return null
}

function accountKind (value) {
  switch (String(value ?? '').toLowerCase()) {
    case 'credit': return 'ccard'
    case 'deposit':
    case 'multideposit': return 'deposit'
    case 'cashloan':
    case 'kupivkredit': return 'loan'
    case 'wallet': return 'emoney'
    default: return 'checking'
  }
}

function bankMessage (json, fallback) {
  return text(json?.plainMessage ?? json?.errorMessage, fallback)
}

async function apiGet (path, sessionId, parameters = {}) {
  if (!/^[a-z0-9_/-]+$/i.test(path)) {
    throw new Error('Некорректный метод T-Банка')
  }
  const url = new URL(`${apiPrefix}${path}`, origin)
  url.searchParams.set('origin', 'web,ib5,platform')
  url.searchParams.set('sessionid', sessionId)
  for (const [key, value] of Object.entries(parameters)) {
    if (value !== undefined && value !== null && value !== '') {
      url.searchParams.set(key, String(value))
    }
  }

  let response
  try {
    response = await fetch(url, {
      method: 'GET',
      headers: {
        Accept: 'application/json',
        'User-Agent': 'WealthTracker/0.1'
      }
    })
  } catch {
    throw new Error('Не удалось связаться с API Т-Банка')
  }
  if (response.status === 401 || response.status === 403) {
    throw new Error('Сессия Т-Банка истекла. Войдите снова через страницу банка.')
  }
  if (!response.ok) {
    throw new Error(`API Т-Банка временно недоступен (${response.status})`)
  }

  let json
  try {
    json = await response.json()
  } catch {
    throw new Error('Т-Банк вернул ответ неизвестного формата')
  }
  if (json?.resultCode !== 'OK') {
    if (json?.resultCode === 'INSUFFICIENT_PRIVILEGES' ||
        json?.resultCode === 'AUTHENTICATION_FAILED') {
      throw new Error('Сессия Т-Банка истекла. Войдите снова через страницу банка.')
    }
    throw new Error(bankMessage(json, `Т-Банк отклонил запрос ${path}`))
  }
  return json.payload
}

function convertAccount (source) {
  const sourceId = text(source?.id)
  const sourceType = String(source?.accountType ?? '')
  if (!sourceId || /^(external|imported|telecom)/i.test(sourceType)) return null
  const available = amount(source?.moneyAmount)
  const creditLimit = amount(source?.creditLimit)
  const statedBalance = amount(source?.accountBalance, source?.balance)
  const debt = amount(source?.debtBalance, source?.debtAmount)
  const credit = sourceType.toLowerCase() === 'credit'
  const balance = credit && available !== null && creditLimit !== null
    ? available - creditLimit
    : statedBalance ?? (credit && debt !== null ? -Math.abs(debt) : available)
  const instrument = currency(
    source?.moneyAmount, source?.accountBalance, source?.creditLimit,
    source?.debtBalance, source?.currency
  )
  if (balance === null || instrument === null) return null

  const syncID = [sourceId]
  if (source.linkedAccountNumber) syncID.push(String(source.linkedAccountNumber))
  for (const card of Array.isArray(source.cards) ? source.cards : []) {
    if (card?.value) syncID.push(String(card.value))
  }
  return {
    id: `tbank:${sourceId}`,
    sourceId,
    title: text(source.name, 'Счёт Т-Банка'),
    type: accountKind(sourceType),
    instrument,
    balance,
    syncID
  }
}

function operationDate (source) {
  const value = source?.operationTime?.milliseconds ??
    source?.debitingTime?.milliseconds ?? source?.date
  const date = typeof value === 'number' || /^\d+$/.test(String(value ?? ''))
    ? new Date(Number(value))
    : new Date(value)
  return Number.isFinite(date.getTime()) ? date : null
}

function description (source) {
  return text(
    source?.merchant?.name ?? source?.brand?.name ??
      source?.description ?? source?.message,
    'Операция Т-Банка'
  )
}

function convertOperation (source, accountId, seenIds) {
  if (!source || source.status === 'FAILED') return null
  const date = operationDate(source)
  const value = amount(source.accountAmount, source.amount)
  if (!date || value === null || value === 0) return null
  const absolute = Math.abs(value)
  const credit = String(source.type ?? '').toLowerCase() === 'credit'
  const title = description(source)
  let sourceId = text(source.id ?? source?.payment?.paymentId)
  if (!sourceId) {
    sourceId = createHash('sha256')
      .update(`${accountId}|${date.toISOString()}|${value}|${title}`)
      .digest('hex')
  }
  const duplicate = seenIds.get(sourceId) ?? 0
  seenIds.set(sourceId, duplicate + 1)
  if (duplicate > 0) sourceId = `${sourceId}:${duplicate}`

  const instrument = currency(source.accountAmount, source.amount) ?? 'RUB'
  const result = {
    id: `tbank:${sourceId}`,
    date,
    income: credit ? absolute : 0,
    outcome: credit ? 0 : absolute,
    incomeAccount: credit ? accountId : `cash#${instrument}`,
    outcomeAccount: credit ? `cash#${instrument}` : accountId,
    comment: title,
    merchant: { fullTitle: title },
    hold: !source.debitingTime
  }
  const mcc = number(source.mcc)
  if (mcc !== null && mcc > 10) result.mcc = Math.trunc(mcc)
  const category = text(source?.spendingCategory?.name ?? source?.category?.name)
  if (category) result.category = category
  return result
}

async function fetchData (sessionId, fromDate, toDate) {
  const rawAccounts = await apiGet('accounts_light_ib', sessionId)
  const convertedAccounts = (Array.isArray(rawAccounts) ? rawAccounts : [])
    .map(convertAccount)
    .filter(Boolean)
  if (convertedAccounts.length === 0) {
    throw new Error('Т-Банк не вернул доступных счетов')
  }

  const seenIds = new Map()
  const transactions = []
  for (const account of convertedAccounts) {
    const operations = await apiGet('operations', sessionId, {
      account: account.sourceId,
      start: fromDate.getTime(),
      end: toDate.getTime()
    })
    for (const operation of Array.isArray(operations) ? operations : []) {
      const converted = convertOperation(operation, account.id, seenIds)
      if (converted) transactions.push(converted)
    }
  }
  return {
    accounts: convertedAccounts.map(({ sourceId, ...account }) => account),
    transactions
  }
}

async function main ({ fromDate, toDate }) {
  const sessionId = ZenMoney.getData(
    'sessionId', ZenMoney.getData('psid', null)
  )
  if (typeof sessionId !== 'string' || sessionId.length < 16 || sessionId.length > 4096) {
    throw new Error('Сначала войдите в Т-Банк через страницу банка')
  }
  const ping = await apiGet('ping', sessionId)
  if (ping?.accessLevel !== 'CLIENT') {
    throw new Error('Сессия Т-Банка истекла. Войдите снова через страницу банка.')
  }
  const data = await fetchData(sessionId, fromDate, toDate)
  ZenMoney.addAccount(data.accounts)
  if (data.transactions.length > 0) ZenMoney.addTransaction(data.transactions)
  ZenMoney.setResult({ success: true })
}

globalThis.main = main

module.exports = {
  accountKind,
  convertAccount,
  convertOperation,
  main
}
