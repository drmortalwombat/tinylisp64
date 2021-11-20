# tinylisp64
 Tiny lisp interpreter for the C64

## Introduction

The AI language of the 50s and 60s live on your Commodore C64.
The **tinylisp64** interpreter offers a small (but sufficient) subset of the full LISP
languages.  Memory is tight and the garbage collector a lazy bumm.

## Operators

The implemented subset includes so far:

* Types: symbols, lists, functions and floats
* Numeric operators: +, -, \*, /
* Numeric comparison: =, /=, <, >, <= and >=
* Basic list handling: cons, car, cdr
* Function definition with lambda and defun
* Variables using let and setq

## Usage

The bottom field allows you to enter S-expressions, which are immediatedly evaluated.

You have some pseudo operators to make live easier:

* (:list) shows all defines symbols
* (:reset) clears the symbol storage
* (:load \[name \[drive]]) loads a workspace from disk
* (:save \[name \[drive]]) saves a workspace to disk
* (:edit name) reloads a definition into the edit area for correction
