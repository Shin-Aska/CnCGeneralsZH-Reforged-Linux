set pagination off
set confirm off
set breakpoint pending on
python
import gdb
seen={'scene':False,'early':False,'warm':False}
class Returned(gdb.FinishBreakpoint):
 def __init__(self,kind,state,fmt):
  super().__init__(internal=True);self.kind=kind;self.state=state;self.fmt=fmt
 def stop(self):
  done=self.state['WarmDone'] if self.kind=='warm' else self.state['Done'][1]
  print('HDR_HAZE_RESULT kind=%s format=%d expectedRGBA16F=29 recorded=%d return=%s'%(self.kind,self.fmt,int(done),self.return_value))
  return False
class Scene(gdb.Breakpoint):
 def stop(self):
  if seen['scene'] or int(gdb.parse_and_eval('TheGameLogic->m_frame'))<3030:return False
  post=gdb.parse_and_eval('this->Post._M_ptr').dereference()
  if not bool(post['InView']):return False
  seen['scene']=True
  print('HDR_SCENE format=%d internalHDR=%d postCount=%d earlyDone=%d weatherStrength=%s warmStrength=%s'%(int(post['Scene']['ColourFormat']),int(post['Settings']['InternalHDR']),int(post['Settings']['Count']),int(post['Atmosphere']['State'].dereference()['Done'][1]),post['Environment']['Haze']['Strength'],post['Environment']['TransitionWarmHaze']['Strength']))
  return False
class Haze(gdb.Breakpoint):
 def __init__(self,kind,spec):super().__init__(spec,internal=True);self.kind=kind
 def stop(self):
  if seen[self.kind] or int(gdb.parse_and_eval('TheGameLogic->m_frame'))<3030:return False
  state=gdb.parse_and_eval('this->State').dereference();strength=state['TransitionWarm']['Strength'] if self.kind=='warm' else state['Ground']['Strength']
  if float(strength)<=0:return False
  seen[self.kind]=True;Returned(self.kind,state,int(gdb.parse_and_eval('scene.ColourFormat')))
  return False
Scene('PosixDevice9::Post_Before_Transparent',internal=True)
Haze('early','SdlPostAtmosphere::Apply_Ground_Haze')
Haze('warm','SdlPostAtmosphere::Apply_Transition_Warm_Haze')
end
run
