set pagination off
set confirm off
set breakpoint pending on
break W3DDisplay::takeScreenShot
commands
silent
set $w=(W3DSnowManager*)TheSnowManager
set $tod=TheWritableGlobalData->m_timeOfDay
printf "LIGHTING frame=%u tod=%d ambient=(%.6g,%.6g,%.6g) diffuse=(%.6g,%.6g,%.6g) weatherTime=%.9g kind=%d E=%.9g\n",TheGameLogic->m_frame,$tod,TheWritableGlobalData->m_terrainLighting[$tod][0].ambient.red,TheWritableGlobalData->m_terrainLighting[$tod][0].ambient.green,TheWritableGlobalData->m_terrainLighting[$tod][0].ambient.blue,TheWritableGlobalData->m_terrainLighting[$tod][0].diffuse.red,TheWritableGlobalData->m_terrainLighting[$tod][0].diffuse.green,TheWritableGlobalData->m_terrainLighting[$tod][0].diffuse.blue,$w->m_weatherTime,$w->m_weatherKind,$w->m_weatherRainEnvelope
printf "LOCAL_PLAYER index=%d type=%d observer=%d\n",ThePlayerList->m_local->m_playerIndex,ThePlayerList->m_local->m_playerType,ThePlayerList->m_local->m_observer
set $sh=TheTerrainRenderObject->m_shroud
set $cx=(int)(640.0/$sh->m_cellWidth)
set $cy=(int)(700.0/$sh->m_cellHeight)
set $pixel=*(unsigned short*)((char*)$sh->m_srcTextureData+$cx*2+$cy*$sh->m_srcTexturePitch)
printf "DRY_LAND_SHROUD level=%g\n",(($pixel>>5)&63)*255.0/63
continue
end
run
