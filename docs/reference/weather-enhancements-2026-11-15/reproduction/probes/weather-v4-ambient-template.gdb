set pagination off
set confirm off
set breakpoint pending on
set $capture=0
break W3DSnowManager::renderWeather
commands
silent
if $capture==0 && this->m_weatherTime>7.0
set $capture=1
call (void)TheAudio->setVolume(0.0,(AudioAffect)0x11)
call (void)TheAudio->setVolume(0.0,(AudioAffect)0x12)
call (void)TheAudio->setVolume(0.0,(AudioAffect)0x14)
call (void)TheAudio->setVolume(0.0,(AudioAffect)0x18)
printf "AMBIENT_START kind=%d time=%g E=%g intensity=%g gust=%g owned=%u baseGain=%g ambient=%g master=%g\n",this->m_weatherKind,this->m_weatherTime,this->m_weatherStormEnvelope,this->m_weatherIntensity,this->m_weatherGust,this->m_weatherAmbientHandle,this->m_weatherAmbientBaseVolume,TheAudio->m_systemAmbientVolume,TheAudio->m_systemMasterVolume
call (int)AIL_ex_start_capture("@WAV@")
end
if $capture==1 && this->m_weatherTime>16.0
set $capture=2
call (void)AIL_ex_stop_capture()
printf "AMBIENT_END kind=%d time=%g E=%g intensity=%g gust=%g owned=%u\n",this->m_weatherKind,this->m_weatherTime,this->m_weatherStormEnvelope,this->m_weatherIntensity,this->m_weatherGust,this->m_weatherAmbientHandle
end
continue
end
run
