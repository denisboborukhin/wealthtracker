// SPDX-License-Identifier: MIT

global.main = async function main () {
  const code = await ZenMoney.readLine('Введите одноразовый код', {
    inputType: 'number',
    time: 10000
  })
  throw new Error(`Банк отклонил код ${code}`)
}
