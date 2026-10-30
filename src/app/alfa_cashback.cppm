module;

// Keep Qt template declarations in the global module on NDK Clang.
extern "C++" {

#include <QDate>
#include <QString>

}

export module wt.alfa_cashback;

export namespace wealthtracker::app {

[[nodiscard]] QString alfa_cashback_start_script(const QDate& today,const QString& runId) {
    static const QString script = QStringLiteral(R"JS((() => {
  const origin = 'https://web.alfabank.ru'
  const page = new URL(location.href)
  if (page.origin !== origin || page.username || page.password) return 'wrong-origin'
  const previous = globalThis.__wealthtrackerCashback
  if (previous) { previous.active=false; previous.controller?.abort() }
  const state = globalThis.__wealthtrackerCashback = {id:'%1',active:true,status:'loading',controller:new AbortController()}
  const current = '%2', next = '%3'
  const monthNames = [
    ['январь','января','январе'],['февраль','февраля','феврале'],['март','марта','марте'],
    ['апрель','апреля','апреле'],['май','мая','мае'],['июнь','июня','июне'],
    ['июль','июля','июле'],['август','августа','августе'],['сентябрь','сентября','сентябре'],
    ['октябрь','октября','октябре'],['ноябрь','ноября','ноябре'],['декабрь','декабря','декабре']]
  const invalid = () => { throw new Error('unsupported-cashback') }
  const active = () => { if (!state.active) invalid() }
  const validText = (text,limit) => typeof text === 'string' && text.length <= limit && !/[\u0000-\u0008\u000b\u000c\u000e-\u001f\u007f-\u009f]/.test(text)
  const monthOf = value => {
    if (!validText(value,80)) invalid()
    const text = value.trim().toLowerCase().replace(/\u00a0/g,' ')
    const iso = text.match(/^(\d{4})-(\d{2})-(\d{2})(?:t[0-9:.+z-]+)?$/)
    const russianDate = text.match(/^(\d{2})\.(\d{2})\.(\d{4})$/)
    if (iso || russianDate) {
      const date = iso ? iso[1]+'-'+iso[2]+'-'+iso[3] : russianDate[3]+'-'+russianDate[2]+'-'+russianDate[1]
      const parsed = new Date(date+'T00:00:00Z')
      if (!Number.isFinite(parsed.getTime()) || parsed.toISOString().slice(0,10) !== date) invalid()
      return date.slice(0,7)+'-01'
    }
    for (const target of [current,next]) {
      const names = monthNames[Number(target.slice(5,7))-1]
      const normalized = text.replace(/^(?:в|на)\s+/,'')
      if (names.some(name => normalized === name || normalized === name+' '+target.slice(0,4))) return target
    }
    invalid()
  }
  const basketId = link => {
    if (!validText(link,4096)) invalid()
    let text = link
    for (let count=0;count<3;++count) {
      try { const decoded=decodeURIComponent(text); if (decoded===text) break; text=decoded } catch (ignored) { invalid() }
    }
    const found = new Set()
    for (const match of text.matchAll(/(?:[?&#]|^)basketOfferId=([A-Za-z0-9_-]{1,128})(?=[&#]|$)/g)) found.add(match[1])
    for (const match of text.matchAll(/"basketOfferId"\s*:\s*"([A-Za-z0-9_-]{1,128})"/g)) found.add(match[1])
    if (found.size !== 1) invalid()
    return Array.from(found)[0]
  }
  const plain = value => {
    if (!validText(value,4096)) invalid()
    let text = value
    if (/[<>&]/.test(text)) {
      const template = document.createElement('template')
      template.innerHTML = text
      if (template.content.querySelector('script,style,iframe,object,embed,img,link,meta')) invalid()
      text = template.content.textContent || ''
    }
    return text.replace(/\s+/g,' ').trim()
  }
  const categoryRow = (category,program) => {
    if (!category || typeof category !== 'object' || typeof category.isConfirmed !== 'boolean') invalid()
    if (!category.isConfirmed) return null
    const title = plain(category.title)
    const subtitle = category.subtitle === undefined || category.subtitle === null ? '' : plain(category.subtitle)
    let text = title
    if (!/%/.test(text) && subtitle) text += ' '+subtitle
    const rates = Array.from(text.matchAll(/(?:до\s+)?([0-9]{1,3}(?:[.,][0-9]{1,2})?)\s*%/gi))
    if (rates.length !== 1) invalid()
    const rate = rates[0]
    let name = text.slice(0,rate.index)+text.slice(rate.index+rate[0].length)
    name = name.replace(/^[\s:–—-]+|[\s:–—-]+$/g,'').trim()
    if (!name && subtitle && !/%/.test(subtitle)) name=subtitle
    if (!name || name.length>100) invalid()
    return {name:name,percent:rate[1],upperBound:/^до\s/i.test(rate[0]),program:program}
  }
  const load = async () => {
    if (!globalThis.newclick_loyalty_ui) {
      await new Promise((resolve,reject) => {
        const element=document.createElement('script')
        element.src=origin+'/newclick-loyalty-ui/assets/remoteEntry.js'
        element.onload=resolve; element.onerror=reject
        document.head.appendChild(element)
      })
    }
    active()
    const remote=globalThis.newclick_loyalty_ui
    if (!remote || typeof remote.get!=='function') invalid()
    const desktop=await remote.get('./desktop')
    if (typeof desktop!=='function') invalid()
    await desktop()
    active()
    const sidepanel=await remote.get('./remotes/side-panels/loyalty/desktop')
    if (typeof sidepanel!=='function') invalid()
    await sidepanel()
    active()
    const chunks=globalThis.webpackChunknewclick_loyalty_ui
    if (!Array.isArray(chunks)) invalid()
    let requireModule
    const entry=[['wealthtracker-'+state.id],{},require => {requireModule=require}]
    chunks.push(entry)
    const index=chunks.indexOf(entry)
    if (index>=0) chunks.splice(index,1)
    if (typeof requireModule!=='function') invalid()
    const prefix=requireModule(81812)
    if (!prefix.e()) prefix.K(requireModule(29876).q())
    return {accounts:requireModule(58481).h,view:requireModule(8670).S}
  }
  let requests=0
  const read = async (method,params,service,path) => {
    active()
    if (++requests>32 || typeof method!=='function' || typeof method.apiDeclaration!=='function' || typeof method.getPath!=='function') invalid()
    const base=new URL(method.getPath()+'/',document.baseURI)
    if (base.origin!==origin || base.pathname!=='/api/'+service+'/') invalid()
    const declaration=method.apiDeclaration(params)
    if (!declaration || declaration.method!=='GET' || declaration.url!==path || declaration.data!==undefined) invalid()
    const response=await method(params,{signal:state.controller.signal,adapter:'fetch',fetchOptions:{redirect:'error'},timeout:8000,maxContentLength:512*1024})
    active()
    if (!response || !response.data || typeof response.data!=='object' || new TextEncoder().encode(JSON.stringify(response.data)).byteLength>512*1024) invalid()
    const serverTime=Date.parse(response.headers?.date)
    if (!Number.isFinite(serverTime) || new Date(serverTime+3*60*60*1000).toISOString().slice(0,7)+'-01'!==current) invalid()
    return response.data
  }
  const mapBounded = async (items,fn) => {
    let position=0
    const result=new Array(items.length)
    await Promise.all(Array.from({length:Math.min(3,items.length)},async () => {
      while (position<items.length) { active(); const index=position++; result[index]=await fn(items[index],index) }
    }))
    return result
  }
  Promise.resolve().then(load).then(async api => {
    const accounts=await read(api.accounts.getAccounts,{},'v1/loyalty','/accounts')
    if (!Array.isArray(accounts.accounts) || accounts.accounts.length>64) invalid()
    const types=new Set()
    for (const entry of accounts.accounts) {
      const type=entry?.account?.typeId
      if (!validText(type,64) || !type) invalid()
      types.add(type)
    }
    if (!types.size || types.size>8) invalid()
    const offers=await mapBounded(Array.from(types),type => read(api.view.getPromotedPrograms,{type:type},'v1/loyalty-view','/promoted/programs'))
    const baskets=new Map(), months=new Map()
    for (const response of offers) {
      if (!Array.isArray(response.programs) || response.programs.length>32) invalid()
      for (const program of response.programs) {
        if (!program || typeof program!=='object') invalid()
        if (program.id!==1) continue
        const month=monthOf(program.date)
        if (month!==current && month!==next) continue
        if (typeof program.confirmed!=='boolean') invalid()
        if (!months.has(month)) months.set(month,{month:month,hasConfirmed:false,categories:[]})
        if (!program.confirmed) continue
        months.get(month).hasConfirmed=true
        const id=basketId(program.deeplink)
        if (baskets.has(id) && baskets.get(id).month!==month) invalid()
        baskets.set(id,{id:id,month:month})
      }
    }
    if (!months.size || baskets.size>16) invalid()
    await mapBounded(Array.from(baskets.values()),async (basket,index) => {
      const data=await read(api.view.getCategoriesV2,{basketOfferId:basket.id},'v1/loyalty-view','/v2/categories')
      const free=data.categoriesSection?.categories
      const paid=data.paidCategoriesSection?.categories
      if ((free===undefined && paid===undefined) || (free!==undefined && !Array.isArray(free)) ||
          (paid!==undefined && !Array.isArray(paid)) || (free?.length||0)+(paid?.length||0)>128) invalid()
      for (const category of [...(free||[]),...(paid||[])]) {
        const row=categoryRow(category,index+1)
        if (row) months.get(basket.month).categories.push(row)
      }
    })
    const result=Array.from(months.values()).map(month => {
      const rates=new Map()
      for (const row of month.categories) {
        const key=row.name.toLowerCase()
        if (!rates.has(key)) rates.set(key,new Set())
        rates.get(key).add(row.percent.replace(',','.')+'|'+row.upperBound)
      }
      const unique=new Map()
      for (const row of month.categories) {
        const name=rates.get(row.name.toLowerCase()).size>1 ? row.name+' · программа '+row.program : row.name
        const key=name.toLowerCase()
        const value={name:name,percent:row.percent,upperBound:row.upperBound}
        if (unique.has(key) && JSON.stringify(unique.get(key))!==JSON.stringify(value)) invalid()
        unique.set(key,value)
      }
      if (unique.size>32) invalid()
      return {month:month.month,selectionRequired:!month.hasConfirmed,categories:Array.from(unique.values())}
    })
    active()
    state.result={version:1,stage:'ready',months:result}
    state.status='ready'
  }).catch(() => { if (state.active) state.status='failed' })
  return state.status
})())JS");
    const QDate current(today.year(),today.month(),1);
    return script.arg(runId,current.toString(Qt::ISODate),current.addMonths(1).toString(Qt::ISODate));
}

} // namespace wealthtracker::app
