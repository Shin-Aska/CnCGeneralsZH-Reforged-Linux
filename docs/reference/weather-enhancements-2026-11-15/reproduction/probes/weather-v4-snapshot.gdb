set pagination off
set confirm off
set breakpoint pending on
break W3DDisplay::takeScreenShot
commands
silent
set $w=(W3DSnowManager*)TheSnowManager
set $tod=TheWritableGlobalData->m_timeOfDay
printf "QUALIFY frame=%u tod=%d classic=%d atmosphere=%d maplight=%d kind=%d on=%d visible=%d time=%.9g E=%g intensity=%g gust=%g wind=(%g,%g) audio=%u legacyRendered=%d\n",TheGameLogic->m_frame,$tod,TheWritableGlobalData->m_classicGraphics,TheWritableGlobalData->m_atmosphere,TheWritableGlobalData->m_mapLight,$w->m_weatherKind,$w->m_weatherOn,$w->m_isVisible,$w->m_weatherTime,$w->m_weatherStormEnvelope,$w->m_weatherIntensity,$w->m_weatherGust,$w->m_weatherWind.x,$w->m_weatherWind.y,$w->m_weatherAmbientHandle,$w->m_totalRendered
printf "LIGHT ambient=(%g,%g,%g) diffuse=(%g,%g,%g)\n",TheWritableGlobalData->m_terrainLighting[$tod][0].ambient.red,TheWritableGlobalData->m_terrainLighting[$tod][0].ambient.green,TheWritableGlobalData->m_terrainLighting[$tod][0].ambient.blue,TheWritableGlobalData->m_terrainLighting[$tod][0].diffuse.red,TheWritableGlobalData->m_terrainLighting[$tod][0].diffuse.green,TheWritableGlobalData->m_terrainLighting[$tod][0].diffuse.blue
printf "HAZE weatherStrength=%g weatherBeforeWater=%d warmStrength=%g skyStrength=%g\n",s_nativeEnvironment.Haze.Strength,s_nativeEnvironment.Haze.BeforeWater,s_nativeEnvironment.TransitionWarmHaze.Strength,s_nativeEnvironment.Atmosphere.Strength
continue
end
run
