module;

// Keep Qt template declarations in the global module on NDK Clang.
extern "C++" {
#include <QDate>
#include <QString>
}

export module wt.ozon_cashback;

export namespace wealthtracker::app {

[[nodiscard]] QString ozon_cashback_start_script(const QDate& today, const QString& runId) {
    static const QString script = QStringLiteral(R"JS((() => {
  const page = new URL(location.href)
  if (page.origin !== 'https://finance.ozon.ru' || page.username || page.password) return 'wrong-origin'
  const old = globalThis.__wealthtrackerCashback
  if (old) { old.active = false; old.controller?.abort() }
  const state = globalThis.__wealthtrackerCashback = {
    id:'%1',status:'loading',active:true,controller:new AbortController(),reasonCode:'',phase:'IMPORT'}
  const current = '%2', next = '%3'
  const active = () => globalThis.__wealthtrackerCashback === state && state.active
  const fail = code => { throw {code:code} }
  const bounded = value => {
    if (!value || typeof value !== 'object' || Array.isArray(value)) fail('OZON-SCHEMA')
    if (new TextEncoder().encode(JSON.stringify(value)).byteLength > 2 * 1024 * 1024) fail('OZON-LIMIT')
    return value
  }
  // MFBonuses' native client owns authentication and its page-local context.
  // This exact fallback asset was verified from the bank's public software.
  const pinned = 'https://cdn2.ozone.ru/s3/bozon-jinx/assets/prod/660d86c1/apps/loyalty/_app/immutable/chunks/MFBonuses.DVjct8rH.js'
  const assets = performance.getEntriesByType('resource').slice(-1024).map(entry => entry.name)
  for (const element of Array.from(document.scripts).slice(0,256)) if (element.src) assets.push(element.src)
  const loaded = assets.reverse().find(value => {
    try {
      const url = new URL(value)
      return url.origin === 'https://cdn2.ozone.ru' && !url.username && !url.password && !url.search && !url.hash &&
        /^\/s3\/bozon-jinx\/assets\/prod\/[a-f0-9]{8,64}\/apps\/loyalty\/_app\/immutable\/chunks\/MFBonuses\.[A-Za-z0-9_-]+\.js$/.test(url.pathname)
    } catch (ignored) { return false }
  })
  const read = (handler,name,path,callPhase,valuePhase) => {
    if (!active()) return Promise.resolve(null)
    if (typeof handler !== 'function' || handler.url !== path ||
        handler.declaration?.operationName !== name || handler.declaration?.query !== '') fail('OZON-MODULE')
    state.phase = callPhase
    return handler.call({}, {signal:state.controller.signal}).then(response => {
      if (!active()) return null
      state.phase = valuePhase
      if (!response || typeof response.isOk !== 'function' || !response.isOk()) fail('OZON-READ')
      return bounded(response.value)
    })
  }
  const statuses = new Set(['ACCEPTED','ACTIVE','FINISHED','NOT_ACCEPTED','PROMOTION_STATUS_ACCEPTED',
    'PROMOTION_STATUS_ACTIVE','PROMOTION_STATUS_CONFIRMATION','PROMOTION_STATUS_ENDED',
    'PROMOTION_STATUS_FINISHED','PROMOTION_STATUS_NOT_ACCEPTABLE_BUT_SHOWN',
    'PROMOTION_STATUS_NOT_ACCEPTED','UNDEFINED_PROMOTION_STATUS'])
  const monthProjection = (source, expected, isCurrent) => {
    if (!source || typeof source !== 'object' || source.period !== expected.slice(0,7) ||
        typeof source.isPromotionsSelected !== 'boolean' || !Array.isArray(source.monthlyPromotions) ||
        source.monthlyPromotions.length > 128 ||
        (isCurrent && typeof source.atLeastOncePromotionsSelected !== 'boolean')) fail('OZON-SCHEMA')
    const categories = [], names = new Set()
    for (const category of source.monthlyPromotions) {
      if (!category || typeof category !== 'object' || !statuses.has(category.status)) fail('OZON-SCHEMA')
      if (category.status !== 'ACCEPTED') continue
      const name = category.title, value = category.cashbackPromotionParams?.decimalPercent
      if (typeof name !== 'string' || !name || name !== name.trim() || name.length > 100 ||
          /[\u0000-\u001f\u007f-\u009f]/.test(name) || names.has(name.toLowerCase()) ||
          (typeof value !== 'number' && typeof value !== 'string')) fail('OZON-SCHEMA')
      const percent = String(value)
      const match = percent.match(/^([0-9]{1,3})(?:\.([0-9]{1,2}))?$/)
      if (!match) fail('OZON-SCHEMA')
      const points = Number(match[1]) * 100 + Number((match[2] || '').padEnd(2,'0'))
      if (points < 1 || points > 10000 || categories.length >= 32) fail('OZON-SCHEMA')
      names.add(name.toLowerCase())
      categories.push({name:name,percent:percent,upperBound:false})
    }
    if ((isCurrent && !source.atLeastOncePromotionsSelected && categories.length) ||
        (source.isPromotionsSelected && !categories.length)) fail('OZON-SCHEMA')
    // An empty future offer list does not prove that next month's choices exist.
    if (!isCurrent && !source.monthlyPromotions.length && !source.isPromotionsSelected) return null
    return {month:expected,selectionRequired:!categories.length && !source.isPromotionsSelected,categories:categories}
  }
  import(loaded || pinned).then(module => {
    if (!active()) return
    state.phase = 'CONTEXT'
    if (module.Kc !== '/apps/loyalty' || !module.rc ||
        typeof module.rc._internal_setSsrStuff !== 'function') fail('OZON-CONTEXT')
    // Match the bank's normal mount procedure before any native accessor.
    // The context stays inside the page and is never included in the projection.
    return fetch('/apps/loyalty/_mf/context', {
      method:'GET',credentials:'same-origin',redirect:'error',signal:state.controller.signal
    }).then(response => {
      if (!active()) return null
      if (!response || !response.ok) fail('OZON-CONTEXT')
      return response.text()
    }).then(text => {
      if (!active()) return
      if (typeof text !== 'string' || text.length > 2 * 1024 * 1024 ||
          new TextEncoder().encode(text).byteLength > 2 * 1024 * 1024) fail('OZON-CONTEXT')
      const context = JSON.parse(text)
      if (!context || typeof context !== 'object' || Array.isArray(context)) fail('OZON-CONTEXT')
      module.rc._internal_setSsrStuff(context)
      return module
    })
  }).then(module => {
    if (!active()) return
    state.phase = 'MODE-CONTRACT'
    if (!module.$s || typeof module.$s.create !== 'function') fail('OZON-MODULE')
    return read(module.Fn,'GetLoyaltyProgramV2','loyalty/bonusState/loyaltyProgramV2','MODE-CALL','MODE-VALUE').then(mode => {
      if (!active()) return
      if (mode.loyaltyProgram === 'STARS' || mode.loyaltyProgram === 'LOYALTY_PROGRAM_KIND_V2_STARS') fail('OZON-STARS')
      if (mode.loyaltyProgram !== 'CASHBACK' && mode.loyaltyProgram !== 'LOYALTY_PROGRAM_KIND_V2_CASHBACK') fail('OZON-PROGRAM')
      state.phase = 'CASHBACK-CREATE'
      const operation = module.$s.create({name:'GetCashbackProgram',gqlNode:false,mapper:value => value})
      if (!operation || typeof operation.getClientHandler !== 'function') fail('OZON-MODULE')
      return read(operation.getClientHandler('loyalty/bonusState/cashbackProgram'),
        'GetCashbackProgram','loyalty/bonusState/cashbackProgram','CASHBACK-CALL','CASHBACK-VALUE')
    })
  }).then(value => {
    if (!active()) return
    const loyalty = bounded(value).me?.client?.loyaltyV2
    if (!loyalty || typeof loyalty !== 'object') fail('OZON-SCHEMA')
    state.phase = 'MONTHS'
    const months = [monthProjection(loyalty.currentMonthlyPromotionsV2,current,true),
      monthProjection(loyalty.futureMonthlyPromotionsV2,next,false)].filter(Boolean)
    state.result = {version:1,stage:'ready',months:months}
    state.status = 'ready'
    state.phase = 'READY'
  }).catch(error => {
    if (!active()) return
    delete state.result
    state.reasonCode = ['OZON-MODULE','OZON-READ','OZON-SCHEMA','OZON-LIMIT','OZON-STARS','OZON-PROGRAM','OZON-CONTEXT']
      .includes(error?.code) ? error.code : state.phase === 'CONTEXT' ? 'OZON-CONTEXT' : 'OZON-READ'
    state.status = 'failed'
  })
  return state.status
})())JS");
    const QDate current(today.year(), today.month(), 1);
    return script.arg(runId, current.toString(Qt::ISODate), current.addMonths(1).toString(Qt::ISODate));
}

} // namespace wealthtracker::app
