import re
def parse(s):
    tok=re.compile(r'\(|\)|"(?:[^"\\]|\\.)*"|[^\s()]+')
    stack=[[]]
    for m in tok.finditer(s):
        t=m.group(0)
        if t=='(':
            stack.append([])
        elif t==')':
            x=stack.pop(); stack[-1].append(x)
        else:
            stack[-1].append(t[1:-1] if t.startswith('"') else t)
    return stack[0][0]
def find(node,name):
    return [c for c in node if isinstance(c,list) and c and c[0]==name]
def one(node,name):
    r=find(node,name); return r[0] if r else None
def prop(fp,key):
    for p in find(fp,'property'):
        if p[1]==key: return p[2]
