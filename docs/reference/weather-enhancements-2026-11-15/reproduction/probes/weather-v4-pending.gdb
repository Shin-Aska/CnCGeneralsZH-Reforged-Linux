set pagination off
set confirm off
set breakpoint pending on
python
import gdb
state={'paused':False,'observed':False,'handle':0,'updates':0,'pausedAdds':0,'matched':False,'cleared':False,'resumed':False,'unobservable':None}
def own_pending(handle):
    visual=gdb.default_visualizer(gdb.parse_and_eval('TheAudio->m_audioRequests'))
    if visual is None: raise RuntimeError('std::list visualizer unavailable')
    for _,ptr in visual.children():
        if not ptr: continue
        req=ptr.dereference()
        if bool(req['m_usePendingEvent']) and req['m_pendingEvent']:
            if int(req['m_pendingEvent'].dereference()['m_playingHandle'])==handle:return True
    return False
class Returned(gdb.FinishBreakpoint):
    def __init__(self):super().__init__(internal=True)
    def stop(self):
        try:
            handle=int(self.return_value)
            if handle<=5:return False
            matched=own_pending(handle)
            print('PENDING_BOUNDARY handle=%d queued=%d frame=%s'%(handle,matched,gdb.parse_and_eval('TheGameLogic->m_frame')))
            if not matched:raise RuntimeError('positive return was not a genuine queued request')
            state.update(observed=True,handle=handle,matched=True)
            gdb.execute('call (void)TheGameLogic->setGamePaused(true,true)',to_string=True)
            state['paused']=True
            state['cleared']=not own_pending(handle)
            print('PENDING_PAUSED queued_removed=%d'%state['cleared'])
        except Exception as error:
            state['unobservable']=str(error);print('PENDING_UNOBSERVABLE '+str(error))
        return False
class Added(gdb.Breakpoint):
    def stop(self):
        try:
            data=gdb.parse_and_eval('eventToAdd->m_eventName.m_data')
            if not data:return False
            name=(data+1).cast(gdb.lookup_type('char').pointer()).string()
            if not name.startswith('WeatherPresentation'):return False
            if bool(gdb.parse_and_eval('TheGameLogic->m_gamePaused')):
                state['pausedAdds']+=1;print('PENDING_PAUSED_ADD '+name)
            if state['observed'] or state['unobservable']:return False
            caller=gdb.newest_frame().older()
            if caller and 'SnowManager::updateWeatherAmbience' in (caller.name() or ''):Returned()
        except Exception as error:
            state['unobservable']=str(error);print('PENDING_UNOBSERVABLE '+str(error))
        return False
class Update(gdb.Breakpoint):
    def stop(self):
        if not state['paused']:return False
        state['updates']+=1
        if state['updates']==20:
            print('PENDING_RETRY_GUARD paused_weather_adds=%d cached_handle=%s'%(state['pausedAdds'],gdb.parse_and_eval('TheSnowManager->m_weatherAmbientHandle')))
            gdb.execute('call (void)TheGameLogic->setGamePaused(false,true)',to_string=True)
            state['paused']=False;state['resumed']=True
        return False
class Shot(gdb.Breakpoint):
    def stop(self):
        print('PENDING_FINAL '+str(state)+' current_handle='+str(gdb.parse_and_eval('TheSnowManager->m_weatherAmbientHandle')))
        return False
Added('AudioManager::addAudioEvent',internal=True)
Update('W3DSnowManager::update',internal=True)
Shot('W3DDisplay::takeScreenShot',internal=True)
end
run
