set pagination off
set confirm off
set breakpoint pending on
break W3DDisplay::takeScreenShot
commands
silent
set $w=(W3DSnowManager*)TheSnowManager
printf "WEATHER_FRAME logic=%u time=%.9g seed=%u envelope=%.9g kind=%d intensity=%.9g gust=%.9g waterModern=%d contactNext=%d\n",TheGameLogic->m_frame,$w->m_weatherTime,$w->m_weatherRainSeed,$w->m_weatherRainEnvelope,$w->m_weatherKind,$w->m_weatherIntensity,$w->m_weatherGust,TheWaterRenderObj->m_useModernWater,$w->m_weatherContactNext
set $i=0
set $water=0
set $land=0
while $i<192
if $w->m_weatherContacts!=0 && $w->m_weatherTime-$w->m_weatherContacts[$i].born<.75 && $w->m_weatherContacts[$i].born>=0
if $w->m_weatherContacts[$i].water
set $water=$water+1
printf "WATER_CONTACT x=%.4g y=%.4g z=%.4g age=%.4g normal=(%.6g,%.6g,%.6g)\n",$w->m_weatherContacts[$i].position.X,$w->m_weatherContacts[$i].position.Y,$w->m_weatherContacts[$i].position.Z,$w->m_weatherTime-$w->m_weatherContacts[$i].born,$w->m_weatherContacts[$i].normal.X,$w->m_weatherContacts[$i].normal.Y,$w->m_weatherContacts[$i].normal.Z
else
if $w->m_weatherTime-$w->m_weatherContacts[$i].born<.24
set $land=$land+1
end
end
end
set $i=$i+1
end
printf "CONTACT_COUNTS water=%d liveLand=%d\n",$water,$land
continue
end
run
