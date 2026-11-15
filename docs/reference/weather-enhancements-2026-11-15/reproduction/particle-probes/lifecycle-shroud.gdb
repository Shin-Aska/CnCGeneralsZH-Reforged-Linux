set pagination off
set confirm off
set breakpoint pending on
set $stage=0
set $nextFrame=600
break W3DSnowManager::render if TheGameLogic->m_frame >= $nextFrame
commands
silent
set $w=(W3DSnowManager*)TheSnowManager
if $stage==0
printf "LIFECYCLE stage=%d frame=%u time=%.9g on=%d visible=%d kind=%d E=%.9g intensity=%.9g audio=%u wind=(%.9g,%.9g)\n",$stage,TheGameLogic->m_frame,$w->m_weatherTime,$w->m_weatherOn,$w->m_isVisible,$w->m_weatherKind,$w->m_weatherRainEnvelope,$w->m_weatherIntensity,$w->m_weatherAmbientHandle,$w->m_weatherWind.x,$w->m_weatherWind.y
set $sh=TheTerrainRenderObject->m_shroud
set $cx=(int)(640.0/$sh->m_cellWidth)
set $cy=(int)(470.0/$sh->m_cellHeight)
set $pixel=*(unsigned short*)((char*)$sh->m_srcTextureData+$cx*2+$cy*$sh->m_srcTexturePitch)
printf "SHROUD_CENTER level=%g\n",(($pixel>>5)&63)*255.0/63
call ((W3DDisplay*)TheDisplay)->takeScreenShot()
set $nextFrame=620
end
if $stage==1
set ((GlobalData*)TheWritableGlobalData)->m_weatherEffects=0
set $nextFrame=621
end
if $stage==2
printf "LIFECYCLE stage=%d frame=%u time=%.9g on=%d visible=%d kind=%d E=%.9g intensity=%.9g audio=%u wind=(%.9g,%.9g)\n",$stage,TheGameLogic->m_frame,$w->m_weatherTime,$w->m_weatherOn,$w->m_isVisible,$w->m_weatherKind,$w->m_weatherRainEnvelope,$w->m_weatherIntensity,$w->m_weatherAmbientHandle,$w->m_weatherWind.x,$w->m_weatherWind.y
set $sh=TheTerrainRenderObject->m_shroud
set $cx=(int)(640.0/$sh->m_cellWidth)
set $cy=(int)(470.0/$sh->m_cellHeight)
set $pixel=*(unsigned short*)((char*)$sh->m_srcTextureData+$cx*2+$cy*$sh->m_srcTexturePitch)
printf "SHROUD_CENTER level=%g\n",(($pixel>>5)&63)*255.0/63
call ((W3DDisplay*)TheDisplay)->takeScreenShot()
set $nextFrame=680
end
if $stage==3
set ((GlobalData*)TheWritableGlobalData)->m_weatherEffects=1
set $nextFrame=681
end
if $stage==4
printf "LIFECYCLE stage=%d frame=%u time=%.9g on=%d visible=%d kind=%d E=%.9g intensity=%.9g audio=%u wind=(%.9g,%.9g)\n",$stage,TheGameLogic->m_frame,$w->m_weatherTime,$w->m_weatherOn,$w->m_isVisible,$w->m_weatherKind,$w->m_weatherRainEnvelope,$w->m_weatherIntensity,$w->m_weatherAmbientHandle,$w->m_weatherWind.x,$w->m_weatherWind.y
set $sh=TheTerrainRenderObject->m_shroud
set $cx=(int)(640.0/$sh->m_cellWidth)
set $cy=(int)(470.0/$sh->m_cellHeight)
set $pixel=*(unsigned short*)((char*)$sh->m_srcTextureData+$cx*2+$cy*$sh->m_srcTexturePitch)
printf "SHROUD_CENTER level=%g\n",(($pixel>>5)&63)*255.0/63
call ((W3DDisplay*)TheDisplay)->takeScreenShot()
set $nextFrame=1060
end
if $stage==5
printf "LIFECYCLE stage=%d frame=%u time=%.9g on=%d visible=%d kind=%d E=%.9g intensity=%.9g audio=%u wind=(%.9g,%.9g)\n",$stage,TheGameLogic->m_frame,$w->m_weatherTime,$w->m_weatherOn,$w->m_isVisible,$w->m_weatherKind,$w->m_weatherRainEnvelope,$w->m_weatherIntensity,$w->m_weatherAmbientHandle,$w->m_weatherWind.x,$w->m_weatherWind.y
set $sh=TheTerrainRenderObject->m_shroud
set $cx=(int)(640.0/$sh->m_cellWidth)
set $cy=(int)(470.0/$sh->m_cellHeight)
set $pixel=*(unsigned short*)((char*)$sh->m_srcTextureData+$cx*2+$cy*$sh->m_srcTexturePitch)
printf "SHROUD_CENTER level=%g\n",(($pixel>>5)&63)*255.0/63
call ((W3DDisplay*)TheDisplay)->takeScreenShot()
set $nextFrame=1080
end
if $stage==6
call $w->setVisible(0)
set $nextFrame=1081
end
if $stage==7
printf "LIFECYCLE stage=%d frame=%u time=%.9g on=%d visible=%d kind=%d E=%.9g intensity=%.9g audio=%u wind=(%.9g,%.9g)\n",$stage,TheGameLogic->m_frame,$w->m_weatherTime,$w->m_weatherOn,$w->m_isVisible,$w->m_weatherKind,$w->m_weatherRainEnvelope,$w->m_weatherIntensity,$w->m_weatherAmbientHandle,$w->m_weatherWind.x,$w->m_weatherWind.y
set $sh=TheTerrainRenderObject->m_shroud
set $cx=(int)(640.0/$sh->m_cellWidth)
set $cy=(int)(470.0/$sh->m_cellHeight)
set $pixel=*(unsigned short*)((char*)$sh->m_srcTextureData+$cx*2+$cy*$sh->m_srcTexturePitch)
printf "SHROUD_CENTER level=%g\n",(($pixel>>5)&63)*255.0/63
call ((W3DDisplay*)TheDisplay)->takeScreenShot()
set $nextFrame=1140
end
if $stage==8
call $w->setVisible(1)
set $nextFrame=1141
end
if $stage==9
printf "LIFECYCLE stage=%d frame=%u time=%.9g on=%d visible=%d kind=%d E=%.9g intensity=%.9g audio=%u wind=(%.9g,%.9g)\n",$stage,TheGameLogic->m_frame,$w->m_weatherTime,$w->m_weatherOn,$w->m_isVisible,$w->m_weatherKind,$w->m_weatherRainEnvelope,$w->m_weatherIntensity,$w->m_weatherAmbientHandle,$w->m_weatherWind.x,$w->m_weatherWind.y
set $sh=TheTerrainRenderObject->m_shroud
set $cx=(int)(640.0/$sh->m_cellWidth)
set $cy=(int)(470.0/$sh->m_cellHeight)
set $pixel=*(unsigned short*)((char*)$sh->m_srcTextureData+$cx*2+$cy*$sh->m_srcTexturePitch)
printf "SHROUD_CENTER level=%g\n",(($pixel>>5)&63)*255.0/63
call ((W3DDisplay*)TheDisplay)->takeScreenShot()
set $nextFrame=1620
end
if $stage==10
printf "LIFECYCLE stage=%d frame=%u time=%.9g on=%d visible=%d kind=%d E=%.9g intensity=%.9g audio=%u wind=(%.9g,%.9g)\n",$stage,TheGameLogic->m_frame,$w->m_weatherTime,$w->m_weatherOn,$w->m_isVisible,$w->m_weatherKind,$w->m_weatherRainEnvelope,$w->m_weatherIntensity,$w->m_weatherAmbientHandle,$w->m_weatherWind.x,$w->m_weatherWind.y
set $sh=TheTerrainRenderObject->m_shroud
set $cx=(int)(640.0/$sh->m_cellWidth)
set $cy=(int)(470.0/$sh->m_cellHeight)
set $pixel=*(unsigned short*)((char*)$sh->m_srcTextureData+$cx*2+$cy*$sh->m_srcTexturePitch)
printf "SHROUD_CENTER level=%g\n",(($pixel>>5)&63)*255.0/63
call ((W3DDisplay*)TheDisplay)->takeScreenShot()
set $nextFrame=1000000000
end
set $stage=$stage+1
continue
end
run
