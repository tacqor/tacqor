class P:
    __slots__=("x","y")
    def __init__(s,x,y): s.x=x; s.y=y
t=0
for i in range(3000000):
    p=P(i,i+1); t+=p.x+p.y
print(t)
