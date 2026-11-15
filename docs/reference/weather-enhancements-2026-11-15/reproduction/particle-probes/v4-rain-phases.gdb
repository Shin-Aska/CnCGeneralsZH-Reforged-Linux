set pagination off
set confirm off
set breakpoint pending on
set $stage=0
set $next=10.0
break W3DSnowManager::render if this->m_weatherTime >= $next
commands
silent
set $w=(W3DSnowManager*)TheSnowManager
printf "PHASE_SAMPLE stage=%d logic=%u time=%.9g seed=%u envelope=%.9g intensity=%.9g gust=%.9g wind=(%.9g,%.9g) audio=%u on=%d visible=%d modern=%d\n",$stage,TheGameLogic->m_frame,$w->m_weatherTime,$w->m_weatherRainSeed,$w->m_weatherRainEnvelope,$w->m_weatherIntensity,$w->m_weatherGust,$w->m_weatherWind.x,$w->m_weatherWind.y,$w->m_weatherAmbientHandle,$w->m_weatherOn,$w->m_isVisible,TheWaterRenderObj->m_useModernWater
if $stage==0
call (int) AIL_ex_start_capture("/mnt/sandisk-2tb/CnCGeneralsZH-Reforged-Linux/build-linux/weather-enhancements-evidence/particle-runtime/v4-rain-cycle-mix.wav")
end
if $stage==5
call (void) AIL_ex_stop_capture()
end
python
try:
    gdb.write("BREEZE_PERIOD script="+str(gdb.parse_and_eval("TheScriptEngine->m_breezeInfo.m_breezePeriod"))+" weather="+str(gdb.parse_and_eval("'Snow.cpp'::s_weatherBreeze.m_breezePeriod"))+"\n")
except gdb.error as error:
    gdb.write("BREEZE_PERIOD_UNOBSERVABLE "+str(error)+"\n")
end
set $i=0
set $live=0
while $i<192
if $w->m_weatherContacts!=0 && $w->m_weatherContacts[$i].born>=0 && $w->m_weatherTime-$w->m_weatherContacts[$i].born<.24
set $live=$live+1
end
set $i=$i+1
end
printf "LIVE_CONTACTS under024=%d\n",$live
call ((W3DDisplay*)TheDisplay)->takeScreenShot()
set $stage=$stage+1
if $stage==1
set $next=50.482704300
end
if $stage==2
set $next=55.538958400
end
if $stage==3
set $next=120.000000000
end
if $stage==4
set $next=227.974471000
end
if $stage==5
set $next=235.253937000
end
if $stage>=6
set $next=1.0e20
end
continue
end
run
