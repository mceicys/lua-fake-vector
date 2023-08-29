# Lua Fake Vector

This is a Lua script preprocessor that adds basic vector functionality (assignment, access, and arithmetic) by duplicating expressions containing vector variable names to create an expression for each component. The idea is to get lightweight vector functionality without garbage collection.

Vectors are indicated with Hungarian notation:

	-- Example
	LFV_EXPAND_VECTORS()
	local v3Test = v3Foo * scalar + Func(v2Bar)

...is expanded to...

	-- Example
	                    
	local  xTest, yTest, zTest =  xFoo * scalar + Func( xBar, yBar), yFoo * scalar + Func( xBar, yBar), zFoo * scalar + Func( xBar, yBar)

There are three vector types:

	v2 =>  x,  y
	v3 =>  x,  y,  z
	q4 => qx, qy, qz, qw

## Setup

The preprocessor is tested with Lua 5.3 but should be compatible with Lua 5.1 thru 5.4.

Include lfv.h to get the declaration for `lfvLoadFile`, which mimics [`luaL_loadfile`](https://www.lua.org/manual/5.3/manual.html#luaL_loadfile) but also does vector expansion. `lfvError` is used to check for expansion errors. For the implementation, compile lfv.c and Lua's source code.

You can also compile lfvutil.c as a console application to get a utility that takes a file's path as input and outputs the expanded version.

For a file to be expanded, its first statement must be `LFV_EXPAND_VECTORS()`, otherwise the preprocessor sends the script to the Lua compiler unmodified. Comments before the header statement are OK. The preprocessor can be forced to expand vectors without the header statement by using the `forceExpand` parameter in `lfvLoadFile` or the /f option in lfvutil. Note that this header function call is erased by the preprocessor, Lua won't execute it unless the file is compiled without being preprocessed.

lfv.h has some other useful functions. Read the comments there for details.

## Limitations

Vectors are not expanded in table constructors.

Vectors are not objects. This is just a shortcut for referring to multiple variables. That means vectors are copied by value, not by reference.

This is not a vector library, so functions like length and cross-product are not inherently available. You'll need a separate library of functions that take and return vectors as individual components:

	LFV_EXPAND_VECTORS()
	
	function Mag3(v3U)
		return math.sqrt(xU * xU + yU * yU + zU * zU)
	end
	
	function Cross(v3U, v3V)
		return yU * zV - zU * yV, zU * xV - xU * zV, xU * yV - yU * xV
	end
	
	-- etc...

Since expressions are duplicated, table accesses and function calls in those expressions are repeated. It's better to first save the results of expensive operations in local variables and use those in the vector expression instead.

There's no function to get the expanded version of a script without sending it to the Lua compiler. That means you can't use your own lua_Reader function.

The preprocessor makes use of some Lua functions and therefore requires a compiled Lua implementation.

lfv.c contains global variables. Multiple threads should not make calls to lfv.h functions.

lfv.c has recursive functions. A script can cause a stack overflow with enough nesting (e.g. argument list within argument list within argument list within...).

While the preprocessor does not heavily modify Lua's syntax (unexpanded files should still compile, though they probably won't run), text editors may get confused. For example, ZeroBrane Studio will underline the component variables `xA, yA` of a local vector `v2A` as if they're globals since they haven't actually been declared.