// SPDX-License-Identifier: MIT

global.main = async function main ({ fromDate }) {
  const code = await ZenMoney.readLine('Введите одноразовый код', {
    inputType: 'number',
    time: 10000
  })
  if (code !== '123456') {
    ZenMoney.setResult({
      success: false,
      message: 'Неверный одноразовый код',
      allowRetry: true
    })
    return
  }

  ZenMoney.setData('session', 'fixture-session')
  ZenMoney.saveData()
  ZenMoney.addAccount({
    id: 'fixture-account',
    title: 'Тестовый счёт',
    type: 'checking',
    instrument: 'RUB',
    balance: 1234.56,
    syncID: ['40817810000000001234']
  })
  ZenMoney.addTransaction({
    id: 'fixture-transaction',
    date: new Date(fromDate.getTime() + 3600000),
    income: 0,
    outcome: 42.75,
    incomeAccount: 'cash#RUB',
    outcomeAccount: 'fixture-account',
    comment: 'Проверка локального моста',
    mcc: 5812
  })
  ZenMoney.setResult({ success: true })
}
