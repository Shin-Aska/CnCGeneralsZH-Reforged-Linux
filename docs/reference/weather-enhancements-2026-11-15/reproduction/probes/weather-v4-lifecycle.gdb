set pagination off
set confirm off
set breakpoint pending on
set $armed=0
set $n=0
set $pausedAdds=0
define weather_state
set $w=(W3DSnowManager*)TheSnowManager
printf "LIFECYCLE n=%d frame=%u paused=%d visible=%d kind=%d on=%d E=%g intensity=%g wind=(%g,%g) audio=%u ambient=%g master=%g\n",$n,TheGameLogic->m_frame,TheGameLogic->m_gamePaused,$w->m_isVisible,$w->m_weatherKind,$w->m_weatherOn,$w->m_weatherStormEnvelope,$w->m_weatherIntensity,$w->m_weatherWind.x,$w->m_weatherWind.y,$w->m_weatherAmbientHandle,TheAudio->m_systemAmbientVolume,TheAudio->m_systemMasterVolume
end
break AudioManager::addAudioEvent
commands
silent
if $armed && TheGameLogic->m_gamePaused
set $pausedAdds=$pausedAdds+1
if eventToAdd->m_eventName.m_data
printf "PAUSED_ADMISSION name=%s\n",(char*)(eventToAdd->m_eventName.m_data+1)
end
end
continue
end
break W3DSnowManager::update
commands
silent
if !$armed && TheGameLogic->m_frame>=720
set $armed=1
end
if $armed
set $n=$n+1
if $n==1
printf "STAGE active\n"
weather_state
end
if $n==10
call (void)TheGameLogic->setGamePaused(true,true)
end
if $n==12
printf "STAGE paused-established\n"
weather_state
end
if $n==14
call (void)TheAudio->removeAudioEvent(this->m_weatherAmbientHandle)
end
if $n==16
call (void)TheAudio->setVolume(0.0,(AudioAffect)0x30)
end
if $n==18
call (void)TheAudio->setVolume(0.7,(AudioAffect)0x30)
end
if $n==20
printf "STAGE paused-unmute-new-admission-must-stay-zero\n"
weather_state
end
if $n==22
call (void)TheGameLogic->setGamePaused(false,true)
end
if $n==30
printf "STAGE resumed\n"
weather_state
end
if $n==32
call (void)TheAudio->loseFocus()
end
if $n==35
printf "STAGE focus-lost\n"
weather_state
end
if $n==38
call (void)TheAudio->regainFocus()
end
if $n==42
printf "STAGE focus-regained\n"
weather_state
end
if $n==45
call (void)this->setVisible(false)
end
if $n==48
printf "STAGE hidden\n"
weather_state
end
if $n==50
call (void)this->setVisible(true)
end
if $n==55
set TheWritableGlobalData->m_weatherEffects=false
end
if $n==58
printf "STAGE weather-off\n"
weather_state
end
if $n==60
set TheWritableGlobalData->m_weatherEffects=true
end
if $n==65
set TheWritableGlobalData->m_classicGraphics=true
end
if $n==68
printf "STAGE classic\n"
weather_state
end
if $n==70
set TheWritableGlobalData->m_classicGraphics=false
end
if $n==74
call (void)this->reset()
printf "STAGE reset-immediate\n"
weather_state
end
if $n==80
printf "STAGE reset-reentry\n"
weather_state
end
if $n==210
printf "STAGE settled-reentry pausedAdds=%d\n",$pausedAdds
weather_state
end
end
continue
end
run
