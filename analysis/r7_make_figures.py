#!/usr/bin/env python3
import csv
import hashlib
import html
import json
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
R6 = REPO / "results" / "v1.1.0" / "analysis_r6"
OUT = REPO / "figures" / "v1.1.0"
OUT.mkdir(parents=True, exist_ok=True)

W, H = 760, 460
M = {"l": 100, "r": 40, "t": 65, "b": 85}


def sha256(path):
    h=hashlib.sha256()
    with open(path,"rb") as f:
        for chunk in iter(lambda:f.read(1<<20),b""):
            h.update(chunk)
    return h.hexdigest()


def load_csv(name):
    return list(csv.DictReader(open(R6/name, newline="")))


def esc(x):
    return html.escape(str(x))


def svg_start(title, subtitle=""):
    s=[f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">']
    s.append('<rect x="0" y="0" width="100%" height="100%" fill="white"/>')
    s.append(f'<text x="{W/2}" y="28" text-anchor="middle" font-family="sans-serif" font-size="18" font-weight="bold">{esc(title)}</text>')
    if subtitle:
        s.append(f'<text x="{W/2}" y="49" text-anchor="middle" font-family="sans-serif" font-size="11">{esc(subtitle)}</text>')
    return s


def finish(s, path):
    s.append('</svg>')
    Path(path).write_text("\n".join(s)+"\n", encoding="utf-8")


def ymap(v, ymin, ymax):
    top=M["t"]; bottom=H-M["b"]
    return bottom - (v-ymin)/(ymax-ymin)*(bottom-top)


def draw_axes(s, ymin, ymax, ylabel, xlabels, zero=True):
    left=M["l"]; right=W-M["r"]; top=M["t"]; bottom=H-M["b"]
    s.append(f'<line x1="{left}" y1="{top}" x2="{left}" y2="{bottom}" stroke="black"/>')
    s.append(f'<line x1="{left}" y1="{bottom}" x2="{right}" y2="{bottom}" stroke="black"/>')
    nt=6
    for i in range(nt):
        v=ymin+(ymax-ymin)*i/(nt-1)
        y=ymap(v,ymin,ymax)
        s.append(f'<line x1="{left-5}" y1="{y:.2f}" x2="{right}" y2="{y:.2f}" stroke="black" stroke-opacity="0.15"/>')
        s.append(f'<text x="{left-9}" y="{y+4:.2f}" text-anchor="end" font-family="sans-serif" font-size="10">{v:.1f}</text>')
    if zero and ymin<0<ymax:
        y=ymap(0,ymin,ymax)
        s.append(f'<line x1="{left}" y1="{y:.2f}" x2="{right}" y2="{y:.2f}" stroke="black" stroke-width="1.4"/>')
    n=len(xlabels)
    xs=[]
    for i,lab in enumerate(xlabels):
        x=left+(right-left)*(i+0.5)/n
        xs.append(x)
        s.append(f'<text x="{x:.2f}" y="{bottom+23}" text-anchor="middle" font-family="sans-serif" font-size="11">{esc(lab)}</text>')
    s.append(f'<text x="24" y="{(top+bottom)/2}" transform="rotate(-90 24 {(top+bottom)/2})" text-anchor="middle" font-family="sans-serif" font-size="12">{esc(ylabel)}</text>')
    return xs


def errorbar_chart(path, title, subtitle, ylabel, xlabels, series, ymin=None, ymax=None):
    vals=[]
    for ser in series:
        vals += list(ser["y"])
        vals += list(ser.get("lo",[]))
        vals += list(ser.get("hi",[]))
    if ymin is None: ymin=min(vals+[0])
    if ymax is None: ymax=max(vals+[0])
    pad=max(0.5,(ymax-ymin)*0.12)
    ymin-=pad; ymax+=pad
    if ymin==ymax: ymin-=1; ymax+=1
    s=svg_start(title,subtitle)
    xs=draw_axes(s,ymin,ymax,ylabel,xlabels)
    offsets=[-9,9,0,-18,18]
    dashes=["","6,3","2,3","10,3,2,3"]
    for j,ser in enumerate(series):
        pts=[]
        off=offsets[j%len(offsets)]
        for i,yv in enumerate(ser["y"]):
            x=xs[i]+off; y=ymap(yv,ymin,ymax)
            if "lo" in ser:
                yl=ymap(ser["lo"][i],ymin,ymax); yh=ymap(ser["hi"][i],ymin,ymax)
                s.append(f'<line x1="{x:.2f}" y1="{yl:.2f}" x2="{x:.2f}" y2="{yh:.2f}" stroke="black"/>')
                s.append(f'<line x1="{x-4:.2f}" y1="{yl:.2f}" x2="{x+4:.2f}" y2="{yl:.2f}" stroke="black"/>')
                s.append(f'<line x1="{x-4:.2f}" y1="{yh:.2f}" x2="{x+4:.2f}" y2="{yh:.2f}" stroke="black"/>')
            pts.append((x,y))
            if j%2==0:
                s.append(f'<circle cx="{x:.2f}" cy="{y:.2f}" r="4" fill="white" stroke="black" stroke-width="1.5"/>')
            else:
                s.append(f'<rect x="{x-4:.2f}" y="{y-4:.2f}" width="8" height="8" fill="white" stroke="black" stroke-width="1.5"/>')
        if len(pts)>1:
            dash=f' stroke-dasharray="{dashes[j%len(dashes)]}"' if dashes[j%len(dashes)] else ""
            s.append('<polyline points="'+' '.join(f'{x:.2f},{y:.2f}' for x,y in pts)+f'" fill="none" stroke="black" stroke-width="1.3"{dash}/>')
    # legend
    lx=W-M["r"]-185; ly=M["t"]+8
    for j,ser in enumerate(series):
        y=ly+j*19
        dash=f' stroke-dasharray="{dashes[j%len(dashes)]}"' if dashes[j%len(dashes)] else ""
        s.append(f'<line x1="{lx}" y1="{y}" x2="{lx+25}" y2="{y}" stroke="black"{dash}/>')
        s.append(f'<text x="{lx+32}" y="{y+4}" font-family="sans-serif" font-size="10">{esc(ser["name"])}</text>')
    finish(s,path)
def bar_chart(path,title,subtitle,ylabel,labels,values,ci=None):
    vals=list(values)
    if ci:
        vals += [x for pair in ci for x in pair]
    ymin=min(vals+[0]); ymax=max(vals+[0]); pad=max(0.5,(ymax-ymin)*0.12)
    ymin-=pad; ymax+=pad
    s=svg_start(title,subtitle)
    xs=draw_axes(s,ymin,ymax,ylabel,labels)
    base=ymap(0,ymin,ymax)
    width=min(46,(W-M["l"]-M["r"])/max(1,len(labels))*0.55)
    for i,v in enumerate(values):
        x=xs[i]; y=ymap(v,ymin,ymax)
        top=min(base,y); height=abs(base-y)
        s.append(f'<rect x="{x-width/2:.2f}" y="{top:.2f}" width="{width:.2f}" height="{height:.2f}" fill="white" stroke="black" stroke-width="1.5"/>')
        if ci:
            lo,hi=ci[i]; yl=ymap(lo,ymin,ymax); yh=ymap(hi,ymin,ymax)
            s.append(f'<line x1="{x:.2f}" y1="{yl:.2f}" x2="{x:.2f}" y2="{yh:.2f}" stroke="black"/>')
            s.append(f'<line x1="{x-4:.2f}" y1="{yl:.2f}" x2="{x+4:.2f}" y2="{yl:.2f}" stroke="black"/>')
            s.append(f'<line x1="{x-4:.2f}" y1="{yh:.2f}" x2="{x+4:.2f}" y2="{yh:.2f}" stroke="black"/>')
        s.append(f'<text x="{x:.2f}" y="{y-7 if v>=0 else y+15:.2f}" text-anchor="middle" font-family="sans-serif" font-size="9">{v:+.1f}%</text>')
    finish(s,path)


def horizontal_bar(path,title,subtitle,labels,values):
    w,h=900,max(520,65+len(labels)*28+70)
    left,right,top,bottom=265,45,70,55
    vmin=min(values+[0]); vmax=max(values+[0]); pad=max(1,(vmax-vmin)*0.08)
    vmin-=pad; vmax+=pad
    def xmap(v): return left+(v-vmin)/(vmax-vmin)*(w-left-right)
    s=[f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">',
       '<rect x="0" y="0" width="100%" height="100%" fill="white"/>',
       f'<text x="{w/2}" y="28" text-anchor="middle" font-family="sans-serif" font-size="18" font-weight="bold">{esc(title)}</text>',
       f'<text x="{w/2}" y="49" text-anchor="middle" font-family="sans-serif" font-size="11">{esc(subtitle)}</text>']
    x0=xmap(0)
    s.append(f'<line x1="{x0:.2f}" y1="{top}" x2="{x0:.2f}" y2="{h-bottom}" stroke="black" stroke-width="1.4"/>')
    for i,(lab,v) in enumerate(zip(labels,values)):
        y=top+20+i*28
        xv=xmap(v)
        x=min(x0,xv); bw=abs(xv-x0)
        s.append(f'<text x="{left-10}" y="{y+4}" text-anchor="end" font-family="sans-serif" font-size="10">{esc(lab)}</text>')
        s.append(f'<rect x="{x:.2f}" y="{y-8}" width="{bw:.2f}" height="16" fill="white" stroke="black"/>')
        s.append(f'<text x="{xv + (6 if v>=0 else -6):.2f}" y="{y+4}" text-anchor="{"start" if v>=0 else "end"}" font-family="sans-serif" font-size="9">{v:+.1f}%</text>')
    s.append(f'<text x="{(left+w-right)/2}" y="{h-15}" text-anchor="middle" font-family="sans-serif" font-size="11">Energy change vs frozen reference (%)</text>')
    s.append('</svg>')
    Path(path).write_text("\n".join(s)+"\n",encoding="utf-8")
def pct_ci(row):
    base=float(row["comparator_mean"])
    return (100*float(row["ci95_low"])/base,100*float(row["ci95_high"])/base)


def main():
    primary=load_csv("primary_summary.csv")
    route=load_csv("primary_route_divergence.csv")
    nrf=load_csv("nrf52840_energy_sensitivity.csv")
    ofat=load_csv("ofat_sensitivity.csv")
    mob=load_csv("mobility_exploratory.csv")

    def pri(comp,endpoint,inter=0):
        rows=[r for r in primary if r["comparator"]==comp and r["endpoint"]==endpoint and int(r["interference"])==inter]
        return sorted(rows,key=lambda r:int(r["relayCap"]))

    caps=["1","2","3"]
    for comp,label,fn in [("hopcount","Hop-count","fig1_energy_vs_hopcount.svg"),
                          ("lqi","LQI","fig2_energy_vs_lqi.svg")]:
        rr=pri(comp,"totalEnergyMWs",0)
        y=[float(r["effect_pct_vs_comparator"]) for r in rr]
        ci=[pct_ci(r) for r in rr]
        errorbar_chart(OUT/fn,f"LEO total-energy effect vs {label}",
                       "Primary static, no interference; 95% hierarchical-bootstrap CI",
                       "LEO minus comparator (%)",caps,
                       [{"name":"Total energy","y":y,"lo":[x[0] for x in ci],"hi":[x[1] for x in ci]}])

    timeout=pri("hopcount","pingTimeouts",0)
    noroute=pri("hopcount","pingNoRoute",0)
    series=[]
    for name,rr in [("Ping timeouts",timeout),("No route",noroute)]:
        cis=[pct_ci(r) for r in rr]
        series.append({"name":name,"y":[float(r["effect_pct_vs_comparator"]) for r in rr],
                       "lo":[x[0] for x in cis],"hi":[x[1] for x in cis]})
    errorbar_chart(OUT/"fig3_availability_vs_hopcount.svg","LEO availability outcomes vs Hop-count",
                   "Primary static, no interference; negative means fewer adverse outcomes",
                   "LEO minus Hop-count (%)",caps,series)

    series=[]
    for comp,label in [("hopcount","vs Hop-count"),("lqi","vs LQI")]:
        rr=sorted([r for r in route if r["comparator"]==comp and int(r["interference"])==0],key=lambda r:int(r["relayCap"]))
        series.append({"name":label,
                       "y":[100*float(r["equal_cell_mean_divergence_fraction"]) for r in rr],
                       "lo":[100*float(r["ci95_low"]) for r in rr],
                       "hi":[100*float(r["ci95_high"]) for r in rr]})
    errorbar_chart(OUT/"fig4_route_divergence.svg","Direct route divergence from LEO",
                   "Jointly successful PINGs; primary static, no interference",
                   "Different routeFingerprint (%)",caps,series,0,45)

    series=[]
    for metric,label in [("hopcount","Hop-count"),("leo","LEO")]:
        rr=sorted([r for r in nrf if r["metric"]==metric and int(r["interference"])==0],key=lambda r:int(r["relayCap"]))
        series.append({"name":label,"y":[float(r["energy_ratio_nrf_over_base"]) for r in rr]})
    errorbar_chart(OUT/"fig5_nrf_energy_ratio.svg","nRF52840 accounting changes energy scale",
                   "Routing is unchanged; ratio is nRF current-table energy / simplified proxy",
                   "Energy ratio (×)",caps,series,10,14)

    ofat_sorted=sorted(ofat,key=lambda r:float(r["energy_effect_pct_vs_reference"]))
    horizontal_bar(OUT/"fig6_ofat_energy_sensitivity.svg","Reconstruction-parameter sensitivity",
                   "OFAT, LEO reference configuration; descriptive sensitivity only",
                   [r["scenarioId"] for r in ofat_sorted],
                   [float(r["energy_effect_pct_vs_reference"]) for r in ofat_sorted])

    labels=[]; vals=[]; cis=[]
    for mode in ["walk","waypoint"]:
        for comp,label in [("hopcount","Hop"),("lqi","LQI")]:
            r=next(r for r in mob if r["mobility"]==mode and r["comparator"]==comp and r["endpoint"]=="totalEnergyMWs")
            labels.append(f"{mode}-{label}")
            vals.append(float(r["effect_pct_vs_comparator"]))
            cis.append(pct_ci(r))
    bar_chart(OUT/"fig7_mobility_energy_exploratory.svg","Mobility extension: LEO energy effect",
              "Exploratory only; not part of primary source-constrained inference",
              "LEO minus comparator (%)",labels,vals,cis)

    # Interference energy: intentionally separate because n=5 scientific cells per cap.
    labels=[]; vals=[]; cis=[]
    for comp,label in [("hopcount","Hop"),("lqi","LQI")]:
        for r in pri(comp,"totalEnergyMWs",1):
            labels.append(f"{label}-cap{r['relayCap']}")
            vals.append(float(r["effect_pct_vs_comparator"]))
            cis.append(pct_ci(r))
    bar_chart(OUT/"fig8_interference_energy_limited.svg","Interference energy effects",
              "Limited evidence: 5 cells/stratum; exact two-sided sign-flip minimum p=0.0625",
              "LEO minus comparator (%)",labels,vals,cis)

    sources={
      "fig1_energy_vs_hopcount.svg":"primary_summary.csv",
      "fig2_energy_vs_lqi.svg":"primary_summary.csv",
      "fig3_availability_vs_hopcount.svg":"primary_summary.csv",
      "fig4_route_divergence.svg":"primary_route_divergence.csv",
      "fig5_nrf_energy_ratio.svg":"nrf52840_energy_sensitivity.csv",
      "fig6_ofat_energy_sensitivity.svg":"ofat_sensitivity.csv",
      "fig7_mobility_energy_exploratory.svg":"mobility_exploratory.csv",
      "fig8_interference_energy_limited.svg":"primary_summary.csv"
    }
    manifest={}
    for fig,src in sources.items():
        manifest[fig]={"figure_sha256":sha256(OUT/fig),"source":src,"source_sha256":sha256(R6/src)}
    (OUT/"FIGURE_PROVENANCE.json").write_text(json.dumps(manifest,indent=2,sort_keys=True)+"\n")
    print("R7_FIGURES=PASS")
    print("FIGURES",len(sources))


if __name__=="__main__":
    main()
