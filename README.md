# Lua Fake Vector

* [Intro](#intro)
* [Setup](#setup)
* [Benchmark](#benchmark)
* [Limitations](#limitations)

## Intro

Lua Fake Vector is a Lua script preprocessor that adds basic vector functionality (assignment, access, and arithmetic) by duplicating expressions containing vector variable names to create an expression for each component. The idea is to get lightweight vector functionality without garbage collection.

Vectors are indicated with Hungarian notation:

```lua
-- Example
LFV_EXPAND_VECTORS()
local v3Test = v3Foo * scalar + Func(v2Bar)
```

...is expanded to...

```lua
-- Example

local  xTest, yTest, zTest =  xFoo * scalar + Func( xBar, yBar), yFoo * scalar + Func( xBar, yBar), zFoo * scalar + Func( xBar, yBar)
```

Vectors are also expanded in table constructors:

```lua
LFV_EXPAND_VECTORS()
t = {v3Example = 1, 2, v2Interrupt = v2Test}
return t.v2Example
```

...becomes...


```lua
                    
t = { xExample = 1, yExample=2,zExample=nil,  xInterrupt =  xTest,yInterrupt= yTest}
return t. xExample,t. yExample
```

There are three vector types:

```
v2 =>  x,  y
v3 =>  x,  y,  z
q4 => qx, qy, qz, qw
```

## Setup

LFV is primarily tested with Lua 5.3 but should be compatible with Lua 5.1 thru 5.4.

Include `lfv.h` and compile `lfv.c` to get the functions `lfvExpandFile` and `lfvExpandString` which take a file path or a string and return the expanded result on success. Free the returned buffer with `lfvFreeBuffer`.

For a file to be expanded, its first statement must be `LFV_EXPAND_VECTORS()`, otherwise the preprocessor leaves the script unmodified. Comments before this statement are OK. The preprocessor can be forced to expand scripts without this statement by passing `1` to the `forceExpand` parameter. Note that this statement is erased by the preprocessor; Lua won't execute it unless the script is compiled without being preprocessed.

Optionally, you may also include `lfvlua.h` and compile `lfvlua.c` to get the functions `lfvLoadFile` and `lfvLoadString`, which mimic [`luaL_loadfile`](https://www.lua.org/manual/5.3/manual.html#luaL_loadfile) and [`luaL_loadstring`](https://www.lua.org/manual/5.3/manual.html#luaL_loadstring). `lfvlua` requires the Lua source and includes its headers with brackets, like this: `#include <lua.h>`

You can also compile `lfvutil.c` (with `lfv.c`) to get a cmd utility that reads from a file or stdin and outputs the expanded version. Parameters are `[/?] [/i inputFile] [/f]`. `/?` displays the help text. `/f` forces expansion.

## Benchmark

The following scripts were executed by Lua 5.4.7 compiled on Visual Studio 2022 17.13.0. The timer had millisecond precision, started right before calling `test` and ended right after. The written time is an average of 100 runs.

### Naive

The left script uses expansion. The right script uses a simple vector class. Note the interface is intuitive in both examples, but the latter generates a lot of expensive garbage.

<table><tr>
<td>

```lua
-- test_lfv.lua: 13.42 ms (%2.84)
LFV_EXPAND_VECTORS()




function test()
	for i = 1, 1000000 do
		local v3A = 1, 2, 3
		local v3B = 4, 5, 6
		local v3C = v3A + v3B
	end
end
```

</td>
<td>

```lua
-- test_tbl.lua: 472.09 ms
local setmetatable = setmetatable
local metaVec3
local vec3 = function(x, y, z) return setmetatable({x, y, z}, metaVec3) end
metaVec3 = {__add = function(u, v) vec3(u[1] + v[1], u[2] + v[2], u[3] + v[3]) end}

function test()
	for i = 1, 1000000 do
		local v3A = vec3(1, 2, 3)
		local v3B = vec3(4, 5, 6)
		local v3C = v3A + v3B
	end
end
```

</td>
</tr></table>

### Relative

As shown in the right example below, a vector library can provide functions that modify an existing vector object instead of returning a new object. That reduces garbage by a lot but still invokes a function call and multiple table accesses for basic arithmetic.

<table><tr>
<td>

```lua
-- test_lfv_rel.lua: 9.1 ms (%25.55)
LFV_EXPAND_VECTORS()

function test()
	
	local v3A = 1, 2, 3
	local v3B = 4, 5, 6
	
	for i = 1, 1000000 do
		v3A = v3A + v3B
	end
end
```

</td>
<td>

```lua
-- test_tbl_rel.lua: 35.62 ms
local vec3Add_ = function(u, v) u[1], u[2], u[3] = u[1] + v[1], u[2] + v[2], u[3] + v[3] end

function test()
	local vec3Add = vec3Add_
	local v3A = {1, 2, 3}
	local v3B = {4, 5, 6}
	
	for i = 1, 1000000 do
		vec3Add(v3A, v3B)
	end
end
```

</td>
</tr></table>

### Globals

Since LFV duplicates an expression for each component, repeatedly accessing a global vector starts to slow things down; 1 hash expands to 3 hashes. LFV is still faster in this example, but the benefit is not as great at this point.

<table><tr>
<td>

```lua
-- test_lfv_global.lua: 29.55 ms (%77.23)
LFV_EXPAND_VECTORS()
v3A = 1, 2, 3
v3B = 4, 5, 6

function test()
	for i = 1, 1000000 do
		v3A = v3A + v3B
	end
end
```

</td>
<td>

```lua
-- test_tbl_global.lua: 38.26 ms
local vec3Add = function(u, v) u[1], u[2], u[3] = u[1] + v[1], u[2] + v[2], u[3] + v[3] end
v3A = {1, 2, 3}
v3B = {4, 5, 6}

function test()
	for i = 1, 1000000 do
		vec3Add(v3A, v3B)
	end
end
```

</td>
</tr></table>

## Limitations

Vectors are not objects. LFV just provides a shortcut for referring to multiple variables.

Since vectors are not objects, vectors are copied by value, not by reference.

Since vectors are not singular objects, functions will typically return a vector as an expression list. An expression calling such a function cannot immediately operate on the returned vector because Lua will truncate the list to a single component. Instead, the returned vector has to be stored temporarily:

```lua
function Normalized2(x, y)
	local m = math.sqrt(x*x + y*y)
	return x / m, y / m -- Vector returned in expression list
end

-- This works OK
v2A = Normalized2(v2A)
v2A = v2A * nNewMag

-- This is wrong; yA will be set to nil
v2A = Normalized2(v2A) * nNewMag
```

Since vector expressions are duplicated, table accesses and function calls in those expressions are repeated. It's better to first save the results in local variables and use those in the vector expression instead:

```lua
-- Expensive; Mag3 is called 3 times
v3A = v3A * Mag3(v3B)   -- xA, yA, zA = xA * Mag3(xB, yB, zB), yA * Mag3(xB, yB, zB), zA * Mag3(xB, yB, zB)

-- Faster
local nMagB = Mag3(v3B) -- local nMagB = Mag3(xB, yB, zB)
v3A = v3A * nMagB       -- xA, yA, zA = xA * nMagB, yA * nMagB, zA * nMagB
```