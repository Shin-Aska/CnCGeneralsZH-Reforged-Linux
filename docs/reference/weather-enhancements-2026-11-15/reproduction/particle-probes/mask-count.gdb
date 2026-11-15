set pagination off
set confirm off
set breakpoint pending on
set $traceStarted=0
python
import gdb,json
metrics={"calls":0,"admitted":0,"rejected":0,"layers":{},"return_unavailable":0}
tracers=[]
class MaskReturn(gdb.FinishBreakpoint):
    def __init__(self,frame,layer):
        self.layer=layer
        super().__init__(frame,internal=True)
    def stop(self):
        try:
            value=float(self.return_value)
            key=str(self.layer)
            bucket=metrics["layers"].setdefault(key,{"calls":0,"admitted":0,"rejected":0})
            metrics["calls"]+=1;bucket["calls"]+=1
            field="admitted" if value>0 else "rejected"
            metrics[field]+=1;bucket[field]+=1
        except Exception:
            metrics["return_unavailable"]+=1
        return False
class MaskEntry(gdb.Breakpoint):
    def stop(self):
        frame=gdb.newest_frame()
        try:layer=int(frame.older().read_var("layerIndex"))
        except Exception:layer=-1
        try:MaskReturn(frame,layer)
        except Exception:metrics["return_unavailable"]+=1
        return False
def start_trace():
    tracers.append(MaskEntry("W3DSnowManager::weatherDustFootprintFade",internal=True))
def end_trace():
    for bp in tracers:bp.enabled=False
    w=gdb.parse_and_eval("(W3DSnowManager*)TheSnowManager").dereference()
    epoch=int(w["m_weatherDustEpoch"]);cells=w["m_weatherDustCells"]
    resolved=dry=arriving=0
    for index in range(4096):
        cell=cells[index]
        if int(cell["epoch"])!=epoch:continue
        resolved+=1
        if bool(cell["dry"]):
            dry+=1
            if float(w["m_weatherTime"])-float(cell["admitted"])<.6:arriving+=1
    metrics.update(resolved_cells=resolved,dry_cells=dry,wet_cells=resolved-dry,arriving_cells=arriving,query_budget_used=int(w["m_weatherDustQueries"]))
    gdb.write("DUST_MASK_RESULT "+json.dumps(metrics,sort_keys=True)+"\n")
end
break W3DSnowManager::renderWeather if $traceStarted==0 && TheGameLogic->m_frame>=600
commands
silent
set $traceStarted=1
python start_trace()
continue
end
break W3DSnowManager::renderWeather if $traceStarted==1 && TheGameLogic->m_frame>=601
commands
silent
python end_trace()
set $traceStarted=2
continue
end
run
