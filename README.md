# Lua Fake Vector

* [Intro](#intro)
* [Build](#build)
	* [Build on Windows with CMake](#build-on-windows-with-cmake)
	* [Build on Linux with make](#build-on-linux-with-make)
	* [Build Manually](#build-manually)
* [Usage](#usage)
	* [Using LFV in Lua](#using-lfv-in-lua)
	* [Using LFV in C](#using-lfv-in-c)
	* [Using lfvutil](#using-lfvutil)
* [Benchmark](#benchmark)
* [Limitations](#limitations)
* [Todo](#todo)
* [Reference](#reference)

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

For a script to be expanded, its first statement must be `LFV_EXPAND_VECTORS()`, otherwise the preprocessor leaves the script unmodified. The preprocessor can be forced to expand scripts via a parameter. Note that this statement is erased by the preprocessor; Lua won't execute it unless the script is compiled without being preprocessed.

## Build

LFV is compatible with Lua 5.1 thru 5.4. You can build it to get:
* lfv: C module dll/so that can be loaded by Lua via `require`
* lfvutil: Command-line utility that reads from a file or stdin and outputs the expanded version

### Build on Windows with CMake

You can build LFV on Windows with CMake 3.22 or higher. After opening a terminal in LFV's directory:

```cmd
mkdir build
cd build
cmake .. -DLUA_DIR="C:/path/to/lua" -DLUA_VERSION="5.4" -DCMAKE_INSTALL_PREFIX="./install"
cmake --build . --config Release
cmake --install .
```

You will find `lfv.dll` in `./install/lib/lua/5.4` and `lfvutil.exe` in `./install/bin`.

### Build on Linux with make

If you have Lua 5.4 installed on your Linux system, you can `cd` to the LFV directory and do:

```bash
sudo make install
```

This builds and installs `lfv.so` in `/usr/local/lib/lua/5.4` and `lfvutil` in `/usr/local/bin`. You can uninstall with:

```bash
sudo make uninstall
```

If you have a different Lua version installed, you can set `LUA_VERSION` to the major.minor version.  
You can set `LUA_DIR` to a preferred directory when searching for Lua's headers.

### Build Manually

```
lfv:
	Windows: lfv.c, lfvlua.c, lfv.def, lua.lib (import library)
	Linux: lfv.c, lfvlua.c
lfvutil:
	lfvutil.c, lfv.c
```

## Usage

### Using LFV in Lua

To load the dynamic library and enable expansion on subsequent `require` calls do:

```lua
lfv = require("lfv").EnsureSearcher()
```

Quick test:

```lua
lfv.LoadString("v3Test = 1, 2, 3; print(1 - v3Test)", true)()
-- Should print 0 -1 -2
```

See the [Reference](#reference) for a description of all the functions.

### Using LFV in C

LFV also has a C API if you want to use the library from C.

Include `lfv.h` to get the functions `lfvExpandFile` and `lfvExpandString` which take a file path or a string and return the expanded result on success. Free the returned buffer with `lfvFreeBuffer`.

Include `lfvlua.h` to get the functions `lfvLoadTextFile` and `lfvLoadString` which mimic [`luaL_loadfile`](https://www.lua.org/manual/5.4/manual.html#luaL_loadfile) and [`luaL_loadstring`](https://www.lua.org/manual/5.4/manual.html#luaL_loadstring). This header also contains the prototypes of C Lua functions registered by `luaopen_lfv`.

### Using lfvutil

`lfvutil` reads from a file or stdin and outputs the expanded version. Parameters are `[-h] [-i inputFile] [-f]`.  
`-h` displays the help text.  
`-i` sets an input file path.  
`-f` forces expansion.

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

:: Vectors are not objects. LFV just provides a shortcut for referring to multiple variables.

:: Since vectors are not objects, vectors are copied by value, not by reference.

:: Since vectors are not singular objects, functions will typically return a vector as an expression list. An expression calling such a function cannot immediately operate on the returned vector because Lua will truncate the list to a single component. Instead, the returned vector has to be stored temporarily:

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

:: Since vector expressions are duplicated, table accesses and function calls in those expressions are repeated. It's better to first save the results in local variables and use those in the vector expression instead:

```lua
-- Expensive; Mag3 is called 3 times
v3A = v3A * Mag3(v3B)   -- xA, yA, zA = xA * Mag3(xB, yB, zB), yA * Mag3(xB, yB, zB), zA * Mag3(xB, yB, zB)

-- Faster
local nMagB = Mag3(v3B) -- local nMagB = Mag3(xB, yB, zB)
v3A = v3A * nMagB       -- xA, yA, zA = xA * nMagB, yA * nMagB, zA * nMagB
```

:: Since a whole vector is stored under multiple names generated at preprocessing time, accessing the vector via a dynamic name string requires runtime string manipulation and a static awareness that multiple keys must be accessed to get the whole vector. LFV is inefficient in this case and currently provides no helper functions to deal with it.

## Todo

* Allow `LFV_EXPAND_VECTORS()` to appear later in the script
* Have `lfv.Searcher` log all expansions to `lfv.sLogPath`
* Return error if different size prefixes show up in a vector expression: `v3A + v2B` is probably not intended
* Return error if not all duplicated components are defined: `v3A = 1, 2` is probably not intended
	* We especially want to catch stuff like: `v2A = Normalized2(v2A) * nNewMag`
* Functions to help with runtime name resolution (string -> component strings, table + string -> multiple fields, etc.)

## Reference

### lfv.LoadTextFile (sFilePath [, bForceExpand] [, sLogPath])
_= CompiledChunk | (nil, sError)_

Loads the text file at `sFilePath` with vector expansion and returns the chunk as a function. If expansion or loading fail, **nil** is returned followed by an error message. Precompiled files are detected and return an error.

If `bForceExpand` is **true**, `LFV_EXPAND_VECTORS()` is not required at the start of the script for expansion to work.

Expansion output and errors are appended to the file located at `sLogPath` if it can be opened/created.

### lfv.LoadString (sChunk [, bForceExpand] [, sLogPath])
_= CompiledChunk | (nil, sError)_

Like [`lfv.LoadTextFile`](#lfvloadtextfile-sfilepath--bforceexpand--slogpath) but takes the script as a string instead of loading it from a file.

### lfv.ExpandFile (sFilePath [, bForceExpand] [, sLogPath])
_= sExpanded | (nil, sError)_

Like [`lfv.LoadTextFile`](#lfvloadtextfile-sfilepath--bforceexpand--slogpath) but returns the expanded script as a string instead of compiling it.

### lfv.ExpandString(sChunk [, bForceExpand] [, sLogPath]);
_= sExpanded | (nil, sError)_

Like [`lfv.ExpandFile`](#lfvexpandfile-sfilepath--bforceexpand--slogpath) but takes the script as a string instead of loading it from a file.

### lfv.EnsureSearcher()
_= lfv_

Inserts [`lfv.Searcher`](#lfvsearchersmodulename) as the second element in [`package.searchers`](https://www.lua.org/manual/5.4/manual.html#pdf-package.searchers) if it doesn't already exist in the array. Typically that's after [`package.preload`](https://www.lua.org/manual/5.4/manual.html#pdf-package.preload) and before the .lua searcher.

The `lfv` module is returned so setup can be done in one statement: `lfv = require("lfv").EnsureSearcher()`

### lfv.Searcher(sModuleName)
_= (Loader, sModulePath) | [sFailReason]_

This function can be inserted into [`package.searchers`](https://www.lua.org/manual/5.4/manual.html#pdf-package.searchers) before the standard .lua file searcher to enable vector expansion on `require`'d scripts.