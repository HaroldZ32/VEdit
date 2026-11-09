import sys; from PIL import Image
d=lambda a,b: max(abs(x-y) for x,y in zip(a,b))
im=Image.open(sys.argv[1]).convert('RGB'); W,H=im.size; p=im.load(); cv=eval(sys.argv[2]); sf=eval(sys.argv[3])
nz=[(x,y) for x in range(0,W,4) for y in range(0,H,4) if p[x,y]!=(0,0,0)]; x0,x1,y0,y1=min(a for a,b in nz),max(a for a,b in nz),min(b for a,b in nz),max(b for a,b in nz)
cx=int(x0+(x1-x0)*0.10); bot=max(y for y in range(y0,y1) if d(p[cx,y],sf)<=2); row=bot-40
lf=next(x for x in range(x0,x1) if d(p[x,row],sf)<=2); rt=next(x-1 for x in range(lf+2,x1) if d(p[x,row],sf)>2)
run=lambda sx,sy,dx,dy: next((i for i in range(24) if d(p[sx+dx*i,sy+dy*i],cv)<3),24)
corner=lambda x: sum(1 for i in range(12) for j in range(12) if d(p[x+i,bot+1-j],cv)<=3)
print(f"{sys.argv[1].split('/')[-1]} win=({x0},{y0},{x1},{y1}) card x=[{lf},{rt}] bottom={bot} stroke_delta={d(p[cx,bot+1],sf)} shadow_down={run(cx,bot+2,0,1)} left={run(lf-2,row,-1,0)} right={run(rt+2,row,1,0)} corner_canvas=left:{corner(lf)} right:{corner(rt-11)}")
