#include "action_broker.hpp"
#include <QtTest>
class BrokerTest:public QObject{Q_OBJECT private slots:
void deniesUnlisted(){ActionBroker::Policy p;p.enabled=true;p.allowSendDanmaku=true;ActionBroker b(p,[](const QString&,std::function<void(bool)>d){d(true);});QCOMPARE(b.submit("x","send_danmaku","hi").error,ActionBroker::Error::Unauthorized);}
void validatesText(){ActionBroker::Policy p;p.enabled=true;p.allowSendDanmaku=true;p.allowedPluginIds.insert("ok");ActionBroker b(p,[](const QString&,std::function<void(bool)>d){d(true);});QCOMPARE(b.submit("ok","send_danmaku"," \n ").error,ActionBroker::Error::InvalidText);QCOMPARE(b.submit("ok","send_danmaku",QString(181,'x')).error,ActionBroker::Error::TooLong);}
void singleFlight(){ActionBroker::Policy p;p.enabled=true;p.allowSendDanmaku=true;p.allowedPluginIds.insert("ok");std::function<void(bool)> pending;int n=0;ActionBroker b(p,[&](const QString&,std::function<void(bool)>d){pending=std::move(d);});QVERIFY(b.submit("ok","send_danmaku","hi",[&](bool){++n;}).accepted);QCOMPARE(b.submit("ok","send_danmaku","again").error,ActionBroker::Error::Busy);pending(true);pending(false);QCOMPARE(n,1);}
};QTEST_GUILESS_MAIN(BrokerTest)
#include "test_action_broker.moc"