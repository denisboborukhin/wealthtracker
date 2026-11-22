import QtQuick
import QtTest
import WealthTracker
TestCase {
 id:test; name:"ConnectionList"; width:1200; height:700; visible:true; when:windowShown
 ListModel {id:rows
  ListElement {connectorId:"ozon-retail";institution:"Ozon Банк";method:"Вход внутри приложения";scope:"Счета и остатки";status:"Сессия сохранена · данные не получены";connected:true;available:true;connectionState:"error"}
  ListElement {connectorId:"vtb-retail";institution:"ВТБ";method:"Вход внутри приложения";scope:"Счета и остатки";status:"Не подключено";connected:false;available:true;connectionState:"disconnected"}
 }
 Component {id:component;ConnectionList {}}
 SignalSpy {id:spy;signalName:"connectionRequested"}
 function test_actions_data(){return [{tag:"phone",w:400},{tag:"desktop",w:1200}]}
 function test_actions(data){
   const list=createTemporaryObject(component,test,{width:data.w,height:650,pageWidth:data.w,model:rows})
   verify(list!==null);wait(100);spy.target=list;spy.clear()
   const button=findChild(list,"connectionAction_ozon-retail")
   verify(button!==null);compare(button.text,"Управление");mouseClick(button)
   compare(spy.count,1);compare(spy.signalArguments[0][0],"ozon-retail")
   compare(spy.signalArguments[0][1],"Ozon Банк");compare(spy.signalArguments[0][4],true)
   const status=findChild(list,"connectionStatus_ozon-retail")
   verify(status.visible);compare(status.tone,"warning")
   rows.setProperty(0,"connectionState","ready");rows.setProperty(0,"status","Данные получены")
   compare(status.tone,"positive");compare(status.text,"Данные получены")
   rows.setProperty(0,"connectionState","error");rows.setProperty(0,"status","Сессия сохранена · данные не получены")
   spy.target=null
 }
}
