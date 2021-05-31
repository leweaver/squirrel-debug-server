class BaseVector {
    constructor(...)
    {
        if (vargv.len() >= 3) {
            x = vargv[0]
            y = vargv[1]
            z = vargv[2]
        }
    }


    x = 0
    y = 0
    z = 0
}

local class Vector3 extends BaseVector {
    function _add(other)
    {
        local cls = this.getclass()
        if (other instanceof cls)
            return cls(x+other.x,y+other.y,z+other.z)
        else
            throw "wrong parameter";
    }
    function Print()
    {
        ::print($"{x}, {y}, {z}\n")
    }
}

local v0 = Vector3(1,2,3)
local v1 = Vector3(11,12,13)
local v2 = v0 + v1
v2.Print()

::FakeNamespace <- {
    Utils = {}
}

FakeNamespace.Utils.SuperClass <- class  {
    constructor(a, b, c, d, e, f) {
        ::print("FakeNamespace.Utils.SuperClass" + a)
    }
}

local myexp = @(a,b) a + b
local mytable = {
    a=10
    b=function(a) { return a+1; }
    c=[9,8,7,6,5,4,3,2,1]
}
local testy = FakeNamespace.Utils.SuperClass("asdf", 123, myexp, ["I'm a string", [1,2,3,4,5,6,7,8]], mytable, BaseVector)