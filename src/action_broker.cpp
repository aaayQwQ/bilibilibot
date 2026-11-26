#include "action_broker.hpp"
#include <QPointer>
#include <atomic>
#include <memory>
#include <utility>
ActionBroker::ActionBroker(Policy p,Sender s,QObject *parent):QObject(parent),policy_(std::move(p)),sender_(std::move(s)){clock_.start();}
QString ActionBroker::errorName(Error e){switch(e){case Error::None:return "none";case Error::Disabled:return "disabled";case Error::Unauthorized:return "unauthorized";case Error::InvalidAction:return "invalid_action";case Error::InvalidText:return "invalid_text";case Error::TooLong:return "too_long";case Error::Busy:return "busy";case Error::RateLimited:return "rate_limited";case Error::SendFailed:return "send_failed";}return "unknown";}
ActionBroker::Result ActionBroker::submit(const QString&id,const QString&action,const QString&text,std::function<void(bool)> done){
 auto reject=[](Error e){return Result{e,false};};
 if(!policy_.enabled||!policy_.allowSendDanmaku||!sender_)return reject(Error::Disabled);
 if(id.isEmpty()||(!policy_.allowedPluginIds.contains(id)&&!policy_.allowedPluginIds.contains("*")))return reject(Error::Unauthorized);
 if(action!="send_danmaku")return reject(Error::InvalidAction);
 if(text.trimmed().isEmpty())return reject(Error::InvalidText);
 QString clean;clean.reserve(text.size());
 for(qsizetype i=0;i<text.size();++i){QChar c=text.at(i);if(c.isHighSurrogate()&&i+1<text.size()&&text.at(i+1).isLowSurrogate()){clean+=c;clean+=text.at(++i);continue;}auto cat=c.category();if(cat==QChar::Other_Control||(cat==QChar::Other_Format&&c.unicode()!=0x200D)||cat==QChar::Other_Surrogate||cat==QChar::Other_NotAssigned)continue;clean+=c;}
 clean=clean.simplified();if(clean.isEmpty())return reject(Error::InvalidText);if(clean.size()>policy_.maxMessageLength)return reject(Error::TooLong);
 if(inFlight_)return reject(Error::Busy);if(lastSuccessMs_>=0&&clock_.elapsed()-lastSuccessMs_<policy_.minIntervalMs)return reject(Error::RateLimited);
 inFlight_=true;QPointer<ActionBroker> guard(this);auto once=std::make_shared<std::atomic_bool>(false);
 try{sender_(clean,[guard,once,done=std::move(done)](bool ok){if(!guard||once->exchange(true))return;guard->finish(ok,done);});}
 catch(...){inFlight_=false;return reject(Error::SendFailed);}return {Error::None,true};
}
void ActionBroker::finish(bool ok,const std::function<void(bool)>&done){if(!inFlight_)return;inFlight_=false;if(ok)lastSuccessMs_=clock_.elapsed();if(done)done(ok);}