# VM

This is my first VM based on [baby's first garbage collector](http://journal.stuffwithstuff.com/2013/12/08/babys-first-garbage-collector/) post.

I've implemented a little VM over the types that the GC manages.
I've also written a little assembler.

The assembler currently supports a bunch of mnemonics and one macro:

## Instructions


```
push <number>       ; push i32 value
pop                 ; pop value from the stack (not accessible anymore)
pair                ; pop two values from the stack and push them as a pair
out                 ; output the bytes saved on the last pushed value recursively, i.e if pair it outputs all the bytes from first and snd
in                  ; pushes the result of `getchar`.
swap                ; swap the last two values of the stack.
gc                  ; force garbage collection.
die <msg>           ; output <msg> to stderr as an error and halt
halt                ; halt the machine.
assert_allocated <n> <msg> ; Used for tests. asserts that the number of allocated objects at the moment is <n>, if not it exits with <msg> as its error.
```

Assembler helpers:
```
print <string> ; print <string> to stdout, including the '\n'.

%repeat <n> [<binding>] ; repeats (statically) the contents of <scope>, optionally binding a value for each
                        ; iteration of the loop. The binding will be set to 0-<n> (not including n) accordingly
                        ; to the current iteration.
<scope>
%end
```

## Syntax

I have made a syntax file for vim/neovim inside the `syntax/` directory. You can install it to see the syntax highlighting.

## Disclaimer

The VM is at a very early stage; it doesn't support any kind of math/conditionals/dynamic loops. The assembly of course
isn't Turing-complete. There is one test which I could not implement using the current set of instructions that the machine has: `test4`.
