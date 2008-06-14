-- (c) Copyright 2008 Rapid Deployment Software - See License.txt
--
--****
-- Category: 
--   search
--
-- Title:
--   Searching
--****

include machine.e

--
-- Internal Function Documentation
--

--**
-- Signature:
--   global function compare(object x1, object x2)
--
-- Description:
--     Compare two items returning less than, equal or greater than.
--
-- Returns:
--     Returns 0 if objects are identical, 1 if x1 is greater, -1 if x1 is less than.
--
-- Comments:
--     Atoms are considered to be less than sequences. Sequences are compared alphabetically 
--     starting with the first element until a difference is found.
--
-- Example 1:
-- x = compare({1,2,{3,{4}},5}, {2-1,1+1,{3,{4}},6-1})
-- -- identical, x is 0
-- 
-- Example 2:
-- if compare("ABC", "ABCD") < 0 then   -- -1
--     -- will be true: ABC is "less" because it is shorter
-- end if
--
-- Example 3:
-- x = compare('a', "a")
-- -- x will be -1 because 'a' is an atom
-- -- while "a" is a sequence
--
-- Example 4:
-- x = compare('a', "a")
-- -- x will be -1 because 'a' is an atom
-- -- while "a" is a sequence
--
-- See Also:
--     search:equal, 
--     <a href="refman_2.htm#23">relational operators</a>,
--     <a href="refman_2.htm#26">operations on sequences</a>
--**

--**
-- Signature:
--     global function equal(object x1, object x2)
--
-- Description:
--     Compare two Euphoria objects to see if they are the same. 
--
-- Returns:
--     Return 1 (true) if they are the same. 
--     Return 0 (false) if they are different.
--
-- Comments:
--     This is equivalent to the expression: compare(x1, x2) = 0
-- 
--     This routine, like most other built-in routines, is very fast. It does not have any 
--     subroutine call overhead.
--
-- Example 1:
-- if equal(PI, 3.14) then
--     puts(1, "give me a better value for PI!\n")
-- end if
--
-- Example 2: 	
-- if equal(name, "George") or equal(name, "GEORGE") then
--    puts(1, "name is George\n")
-- end if
-- 
-- See Also:
--     compare, equals operator (=)
--**

--**
-- Signature:
--     global function find(object needle, object haystack)
--
-- Description:
--     Find needle as an element of haystack. 
--
-- Returns:
--     If successful, return the index of the first element of s that matches. 
--     If unsuccessful return 0.
--
-- Example 1:
-- location = find(11, {5, 8, 11, 2, 3})
-- -- location is set to 3
--
-- Example 2:
-- names = {"fred", "rob", "george", "mary", ""}
-- location = find("mary", names)
-- -- location is set to 4
--
-- See Also:
--     find_from, match, match_from, compare
--**

--**
-- Signature:
--     global function find_from(object needle, object haystack, integer start)
--
-- Description:
--     Find needle as an element of haystack. Start the search at start. 
--
-- Returns:
--     If successful, return the index of the first element of haystack that matches. 
--     If unsuccessful return 0. 
--
-- Comments:
--     start may have any value from 1 to the length of haystack plus 1. (Analogous to the 
--     first index of a slice of haystack).
--
-- Example 1:
-- location = find_from(11, {11, 8, 11, 2, 3}, 2)
-- -- location is set to 3
--
-- Example 2:
-- names = {"mary", "rob", "george", "mary", ""}
-- location = find_from("mary", names, 3)
-- -- location is set to 4
--
-- See Also:
--     find, match, match_from, compare
--**

--**
-- Signature:
--     global function match(sequence needle, sequence haystack)
--
-- Description:
--     Try to match needle against some slice of haystack. 
--
-- Returns:
--     If successful, return the element number of haystack where the (first) matching slice 
--     begins, else return 0.
--
-- Example 1:
-- location = match("pho", "Euphoria")
-- -- location is set to 3
--
-- See Also:
--     find, find_from, compare, match_from, wildcard:wildcard_match
--**

--**
-- Signature:
--     global function match_from(sequence needle, sequence haystack, integer start)
--
-- Description:
--     Try to match needle against some slice of haystack, starting from index start. 
--
-- Returns:
--     If successful, return the element number of haystack where the (first) matching slice 
--     begins, else return 0. 
--
-- Comments:
--     start may have any value from 1 to the length of haystack plus 1. (Just like the first 
--     index of a slice of haystack.)
--
-- Example 1:
-- location = match_from("pho", "phoEuphoria", 4)
-- -- location is set to 6
--
-- See Also:
--     find, find_from, match, compare, wildcard:wildcard_match
--**

--
-- Functions
--

--**
-- Find any element from s1 in s2 starting at position i2 and return
-- its position or 0 if not found.
--
-- Comments:
--   This function may be applied to a string sequence or a complex
--   sequence.
--
-- Example 1:
--   location = find_any("aeiou", "John Smith", 3)
--   -- location is 8
--
-- Example 2:
--   location = find_any("aeiou", "John Doe")
--   -- location is 2

global function find_any(sequence needles, sequence haystack, integer start=1)
	for i = start to length(haystack) do
		if find(haystack[i],needles) then
			return i
		end if
	end for

	return 0
end function
--**

--**
-- Find all needle indexes in haystack optionally starting at start.
--
-- Parameters:
--     needle - object to search for
--     haystack - sequence to search in
--     start - starting index position (defaults to 1)
--
-- Example 1:
-- s = find_all('A', "ABCABAB")
-- -- s is {1,4,6}
--
-- See Also:
--     find, match, match_all

global function find_all(object needle, sequence haystack, integer start=1)
	sequence ret

	ret = {}
	while start > 0 entry do
		ret &= start
		start += 1
	entry
		start = find_from(needle, haystack, start)
	end while
	
	return ret
end function
--**

--**
-- Match all items of haystack in needle.
--
-- Returns:
--     sequence of starting index positions.
--
-- Example 1:
-- s = match_all("the", "the dog chased the cat under the table.")
-- -- s is {1,16,30}
--
-- See Also:
--     match, find, find_all

global function match_all(object needle, sequence haystack, integer start=1)
	sequence ret

	ret = {}
	while start > 0 entry do
		ret &= start
		start += length(needle)
	entry
		start = match_from(needle, haystack, start)
	end while

	return ret
end function
--**

--**
-- Find needle in haystack in reverse order. 
--
-- Parameters:
--     needle - object to search for
--     haystack - sequence to search in
--     start - starting index position (defaults to -1)
--     
-- Returns:
--     If successful, return the index of the first element of haystack that matches. 
--     If unsuccessful return 0.
--
-- Notes:
--     When start is -1, it is changed to length(haystack), i.e. the last item in haystack.
--
-- Example 1:
-- location = rfind(11, {5, 8, 11, 2, 11, 3})
-- -- location is set to 5
--
-- Example 2:
-- names = {"fred", "rob", "rob", "george", "mary"}
-- location = rfind("rob", names)
-- location is set to 3
--
-- See Also:
--     find

global function rfind(object needle, sequence haystack, integer start=-1)
	integer len

	if start = -1 then
		start = length(haystack)
	end if

	len = length(haystack)

	if (start > len) or (len + start < 1) then
		crash("third argument of rfind_from() is out of bounds (%d)", {start})
	end if

	if start < 1 then
		start = len + start
	end if

	for i = start to 1 by -1 do
		if equal(haystack[i], needle) then
			return i
		end if
	end for

	return 0
end function
--**

--**
-- Try to match needle against some slice of haystack in reverse order. 
--
-- Parameters:
--     needle - object to search for
--     haystack - sequence to search in
--     start - starting index position (defaults to -1)
-- 
-- Returns:
--     If successful, return the element number of haystack where the matching slice begins,
--     otherwise return 0.
--
-- Notes:
--     When start is -1, it is changed to length(haystack), i.e. the last item in haystack.
--
-- Example 1:
-- location = rmatch("the", "the dog ate the steak from the table.")
-- -- location is set to 28 (3rd the)
--
-- See Also:
--     rfind, match

global function rmatch(sequence needle, sequence haystack, integer start=-1)
	integer len, lenx

	if start = -1 then
		start = length(haystack)
	end if

	len = length(haystack)
	lenx = length(needle)

	if lenx = 0 then
		crash("first argument of rmatch() must be a non-empty sequence", {})
	elsif (start > len) or  (len + start < 1) then
		crash("third argument of rmatch() is out of bounds (%d)", {start})
	end if

	if start < 1 then
		start = len + start
	end if

	if start + lenx - 1 > len then
		start = len - lenx + 1
	end if

	lenx-= 1

	for i=start to 1 by -1 do
		if equal(needle, haystack[i..i + lenx]) then
			return i
		end if
	end for

	return 0
end function
--**

--**
-- Find needle in up to max slices of haystack and replace with replacement. max can be zero 
-- to mean find/replace all matches.
--
-- Example 1:
-- s = find_replace("the", "THE", "the cat ate the food under the table", 0)
-- -- s is "THE cat ate THE food under THE table"
--
-- Example 2:
-- s = find_replace("the", "THE", "the cat ate the food under the table", 2)
-- -- s is "THE cat ate THE food under the table"

global function find_replace(object needle, object replacement, sequence haystack, 
			integer max=0)
	integer posn, needle_len, replacement_len
	
	if atom(needle) then
		needle = {needle}
	end if
	if atom(replacement) then
		replacement = {replacement}
	end if

	needle_len = length(needle)
	replacement_len = length(replacement)

	posn = match(needle, haystack)
	while posn do
		haystack = haystack[1..posn-1] & replacement & 
			haystack[posn+needle_len .. length(haystack)]
		posn = match_from(needle, haystack, posn + replacement_len)

		max -= 1

		if max = 0 then
			exit
		end if
	end while

	return haystack
end function
--**

--**
-- Assumes haystack is already sorted into ascending order.

global function binary_search(object needle, sequence haystack, integer startpoint = 1, 
			integer endpoint = 0)
	integer lo, hi, mid, c  -- works up to 1.07 billion records
	
	lo = startpoint
	if endpoint <= 0 then
		hi = length(haystack) - endpoint
	else
		hi = endpoint
	end if
	if lo > hi and length(haystack) > 0 then
		hi = length(haystack)
	end if
	mid = startpoint
	c = 0
	while lo <= hi do
		mid = floor((lo + hi) / 2)
		c = compare(needle, haystack[mid])
		if c < 0 then
			hi = mid - 1
		elsif c > 0 then
			lo = mid + 1
		else
			return mid
		end if
	end while
	-- return the position it would have, if inserted now
	if c > 0 then
		mid += 1
	end if
	return -mid
end function
--**

