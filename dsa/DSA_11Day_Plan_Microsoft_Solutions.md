# Microsoft DSA 11-Day Plan: Daywise Problems, Input/Output, Solutions, Complexity

Source plan: `DSA_11Day_Plan_Microsoft.docx`

Primary language: Python. The solutions below are concise interview-ready approaches. For linked-list, tree, and graph problems, assume standard LeetCode-style node classes.

## Day 1 - Arrays + Hashing + Two Pointers

### 1. Two Sum (LC 1)

- Input: `nums = [2,7,11,15], target = 9`
- Output: `[0,1]`
- Solution: Scan once while storing `value -> index` in a hashmap. For each number, check whether `target - num` was seen earlier.
- Runtime Complexity: Time `O(n)`, Space `O(n)`

```python
def two_sum(nums, target):
    seen = {}  # value -> index
    for i in range(len(nums)):
        num = nums[i]
        need = target - num
        if need in seen:
            return [seen[need], i]
        seen[num] = i
    return []
```

### 2. Valid Anagram (LC 242)

- Input: `s = "anagram", t = "nagaram"`
- Output: `true`
- Solution: Count character frequencies in both strings and compare the counts.
- Runtime Complexity: Time `O(n)`, Space `O(k)` where `k` is distinct characters

```python
def is_anagram(s, t):
    if len(s) != len(t):
        return False
    counts = {}
    # count each character in s
    for ch in s:
        if ch in counts:
            counts[ch] += 1
        else:
            counts[ch] = 1
    # subtract each character in t
    for ch in t:
        if ch not in counts:
            return False
        counts[ch] -= 1
        if counts[ch] == 0:
            del counts[ch]
    return len(counts) == 0
```

### 3. Group Anagrams (LC 49)

- Input: `strs = ["eat","tea","tan","ate","nat","bat"]`
- Output: `[["eat","tea","ate"],["tan","nat"],["bat"]]`
- Solution: Build a frequency-count tuple for each word and group words by that tuple in a hashmap.
- Runtime Complexity: Time `O(total characters)`, Space `O(total characters)`

```python
def group_anagrams(strs):
    groups = {}
    for word in strs:
        count = [0] * 26
        for ch in word:
            count[ord(ch) - ord('a')] += 1
        # build a string signature from the counts, e.g. "1#0#2#..."
        signature = ""
        for c in count:
            signature += str(c) + "#"
        if signature not in groups:
            groups[signature] = []
        groups[signature].append(word)
    result = []
    for key in groups:
        result.append(groups[key])
    return result
```

### 4. Move Zeroes (LC 283)

- Input: `nums = [0,1,0,3,12]`
- Output: `[1,3,12,0,0]`
- Solution: Maintain a write pointer for the next non-zero position and swap each non-zero value forward.
- Runtime Complexity: Time `O(n)`, Space `O(1)`

```python
def move_zeroes(nums):
    write = 0
    for read in range(len(nums)):
        if nums[read] != 0:
            nums[write], nums[read] = nums[read], nums[write]
            write += 1
    return nums
```

### 5. Maximum Subarray (LC 53)

- Input: `nums = [-2,1,-3,4,-1,2,1,-5,4]`
- Output: `6`
- Solution: Use Kadane's algorithm: keep the best subarray ending at the current index and the global best.
- Runtime Complexity: Time `O(n)`, Space `O(1)`

```python
def max_subarray(nums):
    best = nums[0]
    cur = nums[0]
    for i in range(1, len(nums)):
        num = nums[i]
        cur = max(num, cur + num)
        best = max(best, cur)
    return best
```

### 6. Merge Sorted Array (LC 88)

- Input: `nums1 = [1,2,3,0,0,0], m = 3, nums2 = [2,5,6], n = 3`
- Output: `[1,2,2,3,5,6]`
- Solution: Fill `nums1` from the back using two pointers at the ends of the initialized portions.
- Runtime Complexity: Time `O(m+n)`, Space `O(1)`

```python
def merge(nums1, m, nums2, n):
    i, j, k = m - 1, n - 1, m + n - 1
    while j >= 0:
        if i >= 0 and nums1[i] > nums2[j]:
            nums1[k] = nums1[i]
            i -= 1
        else:
            nums1[k] = nums2[j]
            j -= 1
        k -= 1
    return nums1
```

### 7. Best Time Buy/Sell Stock (LC 121)

- Input: `prices = [7,1,5,3,6,4]`
- Output: `5`
- Solution: Track the minimum price seen so far and update the best profit at each day.
- Runtime Complexity: Time `O(n)`, Space `O(1)`

```python
def max_profit(prices):
    min_price = float('inf')
    best = 0
    for price in prices:
        min_price = min(min_price, price)
        best = max(best, price - min_price)
    return best
```

### 8. Longest Consecutive Sequence (LC 128)

- Input: `nums = [100,4,200,1,3,2]`
- Output: `4`
- Solution: Put values in a set. Start counting only at numbers where `num - 1` is absent.
- Runtime Complexity: Time `O(n)`, Space `O(n)`

```python
def longest_consecutive(nums):
    num_set = set(nums)
    max_length = 0
    for num in num_set:
        # only start counting from the beginning of a streak
        if num - 1 not in num_set:
            cur_num = num
            cur_length = 1
            while cur_num + 1 in num_set:
                cur_num += 1
                cur_length += 1
            max_length = max(max_length, cur_length)
    return max_length
```

## Day 2 - Strings + Sliding Window

### 1. Longest Substring Without Repeating Characters (LC 3)

- Input: `s = "abcabcbb"`
- Output: `3`
- Solution: Use a sliding window and a map of last seen positions. Move the left pointer past repeated characters.
- Runtime Complexity: Time `O(n)`, Space `O(k)`

```python
def length_of_longest_substring(s):
    seen = set()
    left = 0
    max_len = 0
    for right in range(len(s)):
        # shrink the window until the current char is not repeated
        while s[right] in seen:
            seen.remove(s[left])
            left += 1
        seen.add(s[right])
        cur_len = right - left + 1
        max_len = max(cur_len, max_len)
    return max_len
```

### 2. Longest Palindromic Substring (LC 5)

- Input: `s = "babad"`
- Output: `"bab"` or `"aba"`
- Solution: Expand around every character and every gap between characters, tracking the longest palindrome.
- Runtime Complexity: Time `O(n^2)`, Space `O(1)`

```python
def longest_palindrome(s):
    if len(s) == 0:
        return ""
    start = 0
    end = 0

    def expand(left, right):
        while left >= 0 and right < len(s) and s[left] == s[right]:
            left -= 1
            right += 1
        # step back to the last valid palindrome bounds
        return left + 1, right - 1

    for i in range(len(s)):
        # odd-length palindrome centered at i
        l1, r1 = expand(i, i)
        if r1 - l1 > end - start:
            start = l1
            end = r1
        # even-length palindrome centered between i and i+1
        l2, r2 = expand(i, i + 1)
        if r2 - l2 > end - start:
            start = l2
            end = r2
    return s[start:end + 1]
```

### 3. Minimum Size Subarray Sum (LC 209)

- Input: `target = 7, nums = [2,3,1,2,4,3]`
- Output: `2`
- Solution: Expand the right pointer, then shrink from the left while the window sum is at least target.
- Runtime Complexity: Time `O(n)`, Space `O(1)`

```python
def min_subarray_len(target, nums):
    left = 0
    total = 0
    best = float('inf')
    for right in range(len(nums)):
        total += nums[right]
        while total >= target:
            best = min(best, right - left + 1)
            total -= nums[left]
            left += 1
    return best if best != float('inf') else 0
```

### 4. Subarray Sum Equals K (LC 560)

- Input: `nums = [1,1,1], k = 2`
- Output: `2`
- Solution: Track prefix sums and count how often `prefix - k` has appeared.
- Runtime Complexity: Time `O(n)`, Space `O(n)`

```python
def subarray_sum(nums, k):
    counts = {}
    counts[0] = 1
    prefix = 0
    result = 0
    for num in nums:
        prefix += num
        need = prefix - k
        if need in counts:
            result += counts[need]
        if prefix in counts:
            counts[prefix] += 1
        else:
            counts[prefix] = 1
    return result
```

### 5. Longest Common Prefix (LC 14)

- Input: `strs = ["flower","flow","flight"]`
- Output: `"fl"`
- Solution: Keep shrinking the first string as the prefix until every word starts with it.
- Runtime Complexity: Time `O(total characters)`, Space `O(1)`

```python
def longest_common_prefix(strs):
    if len(strs) == 0:
        return ""
    prefix = ""
    first = strs[0]
    for i in range(len(first)):
        ch = first[i]
        # this character must match at position i in every word
        for word in strs:
            if i >= len(word) or word[i] != ch:
                return prefix
        prefix += ch
    return prefix
```

### 6. Merge Strings Alternately (LC 1768)

- Input: `word1 = "abc", word2 = "pqr"`
- Output: `"apbqcr"`
- Solution: Walk both strings by index and append available characters alternately.
- Runtime Complexity: Time `O(n+m)`, Space `O(n+m)` for output

```python
def merge_alternately(word1, word2):
    result = ""
    i = 0
    while i < len(word1) or i < len(word2):
        if i < len(word1):
            result += word1[i]
        if i < len(word2):
            result += word2[i]
        i += 1
    return result
```

### 7. Valid Palindrome (LC 125)

- Input: `s = "A man, a plan, a canal: Panama"`
- Output: `true`
- Solution: Use two pointers, skip non-alphanumeric characters, and compare lowercased characters.
- Runtime Complexity: Time `O(n)`, Space `O(1)`

```python
def is_palindrome(s):
    left, right = 0, len(s) - 1
    while left < right:
        while left < right and not s[left].isalnum():
            left += 1
        while left < right and not s[right].isalnum():
            right -= 1
        if s[left].lower() != s[right].lower():
            return False
        left += 1
        right -= 1
    return True
```

## Day 3 - Binary Search All Variants

### 1. Binary Search (LC 704)

- Input: `nums = [-1,0,3,5,9,12], target = 9`
- Output: `4`
- Solution: Use classic binary search with `lo`, `hi`, and `mid`; discard half of the search range each step.
- Runtime Complexity: Time `O(log n)`, Space `O(1)`

```python
def search(nums, target):
    lo, hi = 0, len(nums) - 1
    while lo <= hi:
        mid = (lo + hi) // 2
        if nums[mid] == target:
            return mid
        if nums[mid] < target:
            lo = mid + 1
        else:
            hi = mid - 1
    return -1
```

### 2. Median of Two Sorted Arrays (LC 4)

- Input: `nums1 = [1,3], nums2 = [2]`
- Output: `2.0`
- Solution: Binary search the partition in the smaller array so left halves contain half the elements and all left values are <= all right values.
- Runtime Complexity: Time `O(log min(n,m))`, Space `O(1)`

```python
def find_median_sorted_arrays(nums1, nums2):
    if len(nums1) > len(nums2):
        nums1, nums2 = nums2, nums1
    n, m = len(nums1), len(nums2)
    half = (n + m + 1) // 2
    lo, hi = 0, n
    while lo <= hi:
        i = (lo + hi) // 2  # cut in nums1
        j = half - i        # cut in nums2
        l1 = nums1[i - 1] if i > 0 else float('-inf')
        r1 = nums1[i] if i < n else float('inf')
        l2 = nums2[j - 1] if j > 0 else float('-inf')
        r2 = nums2[j] if j < m else float('inf')
        if l1 <= r2 and l2 <= r1:
            if (n + m) % 2:
                return float(max(l1, l2))
            return (max(l1, l2) + min(r1, r2)) / 2
        elif l1 > r2:
            hi = i - 1
        else:
            lo = i + 1
```

### 3. Search in Rotated Sorted Array (LC 33)

- Input: `nums = [4,5,6,7,0,1,2], target = 0`
- Output: `4`
- Solution: Binary search; at each step identify which half is sorted and decide whether target lies there.
- Runtime Complexity: Time `O(log n)`, Space `O(1)`

```python
def search_rotated(nums, target):
    lo, hi = 0, len(nums) - 1
    while lo <= hi:
        mid = (lo + hi) // 2
        if nums[mid] == target:
            return mid
        if nums[lo] <= nums[mid]:  # left half sorted
            if nums[lo] <= target < nums[mid]:
                hi = mid - 1
            else:
                lo = mid + 1
        else:  # right half sorted
            if nums[mid] < target <= nums[hi]:
                lo = mid + 1
            else:
                hi = mid - 1
    return -1
```

### 4. Find Minimum in Rotated Array (LC 153)

- Input: `nums = [3,4,5,1,2]`
- Output: `1`
- Solution: Binary search against the right boundary; if `nums[mid] > nums[right]`, the minimum is to the right.
- Runtime Complexity: Time `O(log n)`, Space `O(1)`

```python
def find_min(nums):
    lo, hi = 0, len(nums) - 1
    while lo < hi:
        mid = (lo + hi) // 2
        if nums[mid] > nums[hi]:
            lo = mid + 1
        else:
            hi = mid
    return nums[lo]
```

### 5. Koko Eating Bananas (LC 875)

- Input: `piles = [3,6,7,11], h = 8`
- Output: `4`
- Solution: Binary search eating speed. For a candidate speed, compute required hours using ceiling division.
- Runtime Complexity: Time `O(n log max(piles))`, Space `O(1)`

```python
def min_eating_speed(piles, h):
    lo = 1
    hi = max(piles)
    while lo < hi:
        speed = (lo + hi) // 2
        # count total hours needed at this eating speed
        hours = 0
        for p in piles:
            # ceiling division without importing math
            hours += (p + speed - 1) // speed
        if hours <= h:
            hi = speed
        else:
            lo = speed + 1
    return lo
```

### 6. Find Peak Element (LC 162)

- Input: `nums = [1,2,1,3,5,6,4]`
- Output: `1` or `5`
- Solution: Binary search the slope. If `nums[mid] < nums[mid+1]`, a peak exists to the right; otherwise left.
- Runtime Complexity: Time `O(log n)`, Space `O(1)`

```python
def find_peak_element(nums):
    lo, hi = 0, len(nums) - 1
    while lo < hi:
        mid = (lo + hi) // 2
        if nums[mid] < nums[mid + 1]:
            lo = mid + 1
        else:
            hi = mid
    return lo
```

## Day 4 - Design + Stack + Monotonic

### 1. Valid Parentheses (LC 20)

- Input: `s = "()[]{}"`
- Output: `true`
- Solution: Push opening brackets on a stack. For each closing bracket, pop and verify the matching opener.
- Runtime Complexity: Time `O(n)`, Space `O(n)`

```python
def is_valid(s):
    pairs = {')': '(', ']': '[', '}': '{'}
    stack = []
    for ch in s:
        if ch in pairs:
            if not stack or stack.pop() != pairs[ch]:
                return False
        else:
            stack.append(ch)
    return not stack
```

### 2. Min Stack (LC 155)

- Input: `push(-2), push(0), push(-3), getMin(), pop(), top(), getMin()`
- Output: `-3, 0, -2`
- Solution: Maintain a normal stack plus a min stack that stores the current minimum whenever it changes.
- Runtime Complexity: Time `O(1)` per operation, Space `O(n)`

```python
class MinStack:
    def __init__(self):
        self.stack = []
        self.mins = []

    def push(self, val):
        self.stack.append(val)
        if not self.mins or val <= self.mins[-1]:
            self.mins.append(val)

    def pop(self):
        val = self.stack.pop()
        if val == self.mins[-1]:
            self.mins.pop()

    def top(self):
        return self.stack[-1]

    def getMin(self):
        return self.mins[-1]
```

### 3. Daily Temperatures (LC 739)

- Input: `temperatures = [73,74,75,71,69,72,76,73]`
- Output: `[1,1,4,2,1,1,0,0]`
- Solution: Keep a decreasing stack of indices. When a warmer day arrives, pop colder indices and fill wait times.
- Runtime Complexity: Time `O(n)`, Space `O(n)`

```python
def daily_temperatures(temperatures):
    n = len(temperatures)
    result = [0] * n
    stack = []  # indices with decreasing temperatures
    for i in range(n):
        temp = temperatures[i]
        while len(stack) > 0 and temperatures[stack[-1]] < temp:
            prev = stack.pop()
            result[prev] = i - prev
        stack.append(i)
    return result
```

### 4. LRU Cache (LC 146)

- Input: `LRUCache(2), put(1,1), put(2,2), get(1), put(3,3), get(2)`
- Output: `1, -1`
- Solution: Use a hashmap from key to doubly linked-list node. Move accessed nodes to the front and evict from the tail.
- Runtime Complexity: Time `O(1)` per get/put, Space `O(capacity)`

```python
class Node:
    def __init__(self, key, value):
        self.key = key
        self.value = value
        self.prev = None
        self.next = None

class LRUCache:
    def __init__(self, capacity):
        self.capacity = capacity
        self.cache = {}  # key -> Node
        # dummy head and tail so we never handle None edges
        self.head = Node(0, 0)
        self.tail = Node(0, 0)
        self.head.next = self.tail
        self.tail.prev = self.head

    def _remove(self, node):
        node.prev.next = node.next
        node.next.prev = node.prev

    def _add_to_front(self, node):
        node.next = self.head.next
        node.prev = self.head
        self.head.next.prev = node
        self.head.next = node

    def get(self, key):
        if key not in self.cache:
            return -1
        node = self.cache[key]
        # move the used node to the front
        self._remove(node)
        self._add_to_front(node)
        return node.value

    def put(self, key, value):
        if key in self.cache:
            old = self.cache[key]
            self._remove(old)
        node = Node(key, value)
        self.cache[key] = node
        self._add_to_front(node)
        if len(self.cache) > self.capacity:
            # evict the least-recently-used node (just before tail)
            lru = self.tail.prev
            self._remove(lru)
            del self.cache[lru.key]
```

### 5. LRU + Thread-Safety Follow-up

- Input: `multiple threads call get/put on shared cache`
- Output: `correct LRU behavior without races`
- Solution: Wrap get/put in a lock for correctness. For higher throughput, shard the cache by `hash(key) % shard_count`, each with its own lock.
- Runtime Complexity: Time `O(1)` average per operation plus lock contention, Space `O(capacity)`

```python
import threading

class ThreadSafeLRU:
    def __init__(self, capacity):
        self.lru = LRUCache(capacity)  # reuse the DLL-based LRU above
        self.lock = threading.Lock()

    def get(self, key):
        with self.lock:
            return self.lru.get(key)

    def put(self, key, value):
        with self.lock:
            self.lru.put(key, value)

# For higher throughput: shard into N ThreadSafeLRU instances,
# routing each key by hash(key) % N so locks rarely contend.
```

### 6. LFU Cache (LC 460)

- Input: `LFUCache(2), put(1,1), put(2,2), get(1), put(3,3), get(2), get(3)`
- Output: `1, -1, 3`
- Solution: Store `key -> (value, frequency)` and `frequency -> ordered keys`. Evict the least frequently used and oldest key in the minimum-frequency bucket.
- Runtime Complexity: Time `O(1)` average per get/put, Space `O(capacity)`

```python
class DNode:
    def __init__(self, key, value):
        self.key = key
        self.value = value
        self.freq = 1
        self.prev = None
        self.next = None

class DList:
    """Doubly linked list; most-recently-used stays at the front."""
    def __init__(self):
        self.head = DNode(0, 0)
        self.tail = DNode(0, 0)
        self.head.next = self.tail
        self.tail.prev = self.head
        self.size = 0

    def add_to_front(self, node):
        node.next = self.head.next
        node.prev = self.head
        self.head.next.prev = node
        self.head.next = node
        self.size += 1

    def remove(self, node):
        node.prev.next = node.next
        node.next.prev = node.prev
        self.size -= 1

    def remove_last(self):
        if self.size == 0:
            return None
        last = self.tail.prev
        self.remove(last)
        return last

class LFUCache:
    def __init__(self, capacity):
        self.capacity = capacity
        self.key_to_node = {}    # key -> DNode
        self.freq_to_list = {}   # freq -> DList of nodes at that freq
        self.min_freq = 0

    def _bump(self, node):
        # move a node from its current freq list to freq+1 list
        freq = node.freq
        self.freq_to_list[freq].remove(node)
        if self.freq_to_list[freq].size == 0:
            del self.freq_to_list[freq]
            if self.min_freq == freq:
                self.min_freq += 1
        node.freq += 1
        if node.freq not in self.freq_to_list:
            self.freq_to_list[node.freq] = DList()
        self.freq_to_list[node.freq].add_to_front(node)

    def get(self, key):
        if key not in self.key_to_node:
            return -1
        node = self.key_to_node[key]
        self._bump(node)
        return node.value

    def put(self, key, value):
        if self.capacity == 0:
            return
        if key in self.key_to_node:
            node = self.key_to_node[key]
            node.value = value
            self._bump(node)
            return
        if len(self.key_to_node) >= self.capacity:
            # evict least-frequently-used, oldest within that freq
            lru_list = self.freq_to_list[self.min_freq]
            evict = lru_list.remove_last()
            del self.key_to_node[evict.key]
            if lru_list.size == 0:
                del self.freq_to_list[self.min_freq]
        node = DNode(key, value)
        self.key_to_node[key] = node
        if 1 not in self.freq_to_list:
            self.freq_to_list[1] = DList()
        self.freq_to_list[1].add_to_front(node)
        self.min_freq = 1
```

### 7. Design In-Memory File System (LC 588)

- Input: `mkdir("/a/b/c"), addContentToFile("/a/b/c/d","hello"), ls("/"), readContentFromFile("/a/b/c/d")`
- Output: `["a"], "hello"`
- Solution: Model directories/files as trie nodes. Directory nodes contain child maps; file nodes store content.
- Runtime Complexity: Time `O(path length + listing sort)`, Space `O(total nodes + content)`

```python
class Node:
    def __init__(self):
        self.children = {}
        self.content = None  # None => directory, str => file

class FileSystem:
    def __init__(self):
        self.root = Node()

    def _traverse(self, path):
        node = self.root
        if path == "/":
            return node
        parts = path.strip("/").split("/")
        for part in parts:
            if part not in node.children:
                node.children[part] = Node()
            node = node.children[part]
        return node

    def ls(self, path):
        node = self._traverse(path)
        if node.content is not None:  # it's a file
            parts = path.strip("/").split("/")
            return [parts[-1]]
        names = []
        for name in node.children:
            names.append(name)
        names.sort()
        return names

    def mkdir(self, path):
        self._traverse(path)

    def addContentToFile(self, filePath, content):
        node = self._traverse(filePath)
        if node.content is None:
            node.content = ""
        node.content += content

    def readContentFromFile(self, filePath):
        node = self._traverse(filePath)
        return node.content
```

## Day 5 - Linked List

> All solutions assume `class ListNode: def __init__(self, val=0, next=None): self.val, self.next = val, next`.

### 1. Reverse Linked List (LC 206)

- Input: `head = [1,2,3,4,5]`
- Output: `[5,4,3,2,1]`
- Solution: Iterate with `prev`, `curr`, and `next`; reverse one pointer at a time.
- Runtime Complexity: Time `O(n)`, Space `O(1)`

```python
def reverse_list(head):
    prev = None
    curr = head
    while curr:
        nxt = curr.next
        curr.next = prev
        prev = curr
        curr = nxt
    return prev
```

### 2. Merge Two Sorted Lists (LC 21)

- Input: `list1 = [1,2,4], list2 = [1,3,4]`
- Output: `[1,1,2,3,4,4]`
- Solution: Use a dummy head and append the smaller current node until one list ends, then attach the remainder.
- Runtime Complexity: Time `O(n+m)`, Space `O(1)`

```python
def merge_two_lists(list1, list2):
    dummy = ListNode()
    tail = dummy
    while list1 and list2:
        if list1.val <= list2.val:
            tail.next = list1
            list1 = list1.next
        else:
            tail.next = list2
            list2 = list2.next
        tail = tail.next
    # attach whatever remains
    if list1:
        tail.next = list1
    else:
        tail.next = list2
    return dummy.next
```

### 3. Linked List Cycle (LC 141)

- Input: `head = [3,2,0,-4], pos = 1`
- Output: `true`
- Solution: Use Floyd's fast/slow pointers. If they meet, a cycle exists.
- Runtime Complexity: Time `O(n)`, Space `O(1)`

```python
def has_cycle(head):
    slow = fast = head
    while fast and fast.next:
        slow = slow.next
        fast = fast.next.next
        if slow is fast:
            return True
    return False
```

### 4. Linked List Cycle II (LC 142)

- Input: `head = [3,2,0,-4], pos = 1`
- Output: `node with value 2`
- Solution: After fast/slow meet, reset one pointer to head and move both one step; the meeting point is cycle start.
- Runtime Complexity: Time `O(n)`, Space `O(1)`

```python
def detect_cycle(head):
    slow = fast = head
    while fast and fast.next:
        slow = slow.next
        fast = fast.next.next
        if slow is fast:
            ptr = head
            while ptr is not slow:
                ptr = ptr.next
                slow = slow.next
            return ptr
    return None
```

### 5. Remove Nth From End (LC 19)

- Input: `head = [1,2,3,4,5], n = 2`
- Output: `[1,2,3,5]`
- Solution: Use a dummy node. Move fast pointer `n` steps ahead, then move fast/slow together until fast reaches the end.
- Runtime Complexity: Time `O(n)`, Space `O(1)`

```python
def remove_nth_from_end(head, n):
    dummy = ListNode(0, head)
    fast = slow = dummy
    for _ in range(n):
        fast = fast.next
    while fast.next:
        fast = fast.next
        slow = slow.next
    slow.next = slow.next.next
    return dummy.next
```

### 6. Add Two Numbers (LC 2)

- Input: `l1 = [2,4,3], l2 = [5,6,4]`
- Output: `[7,0,8]`
- Solution: Traverse both lists with carry, creating one output digit per step.
- Runtime Complexity: Time `O(max(n,m))`, Space `O(max(n,m))` for output

```python
def add_two_numbers(l1, l2):
    dummy = ListNode()
    tail = dummy
    carry = 0
    while l1 or l2 or carry:
        total = carry
        if l1:
            total += l1.val
            l1 = l1.next
        if l2:
            total += l2.val
            l2 = l2.next
        carry = total // 10
        digit = total % 10
        tail.next = ListNode(digit)
        tail = tail.next
    return dummy.next
```

### 7. Merge K Sorted Lists (LC 23)

- Input: `lists = [[1,4,5],[1,3,4],[2,6]]`
- Output: `[1,1,2,3,4,4,5,6]`
- Solution: Push each list head into a min-heap. Repeatedly pop the smallest node and push its next node.
- Runtime Complexity: Time `O(N log k)`, Space `O(k)`

```python
import heapq

def merge_k_lists(lists):
    heap = []
    for i in range(len(lists)):
        node = lists[i]
        if node:
            heapq.heappush(heap, (node.val, i, node))
    dummy = ListNode()
    tail = dummy
    while len(heap) > 0:
        val, i, node = heapq.heappop(heap)
        tail.next = node
        tail = tail.next
        if node.next:
            heapq.heappush(heap, (node.next.val, i, node.next))
    return dummy.next
```

### 8. Copy List with Random Pointer (LC 138)

- Input: `head = [[7,null],[13,0],[11,4],[10,2],[1,0]]`
- Output: `deep copy with same next/random structure`
- Solution: First map each original node to a copied node, then wire `next` and `random` pointers using the map.
- Runtime Complexity: Time `O(n)`, Space `O(n)`

```python
def copy_random_list(head):
    if not head:
        return None
    mapping = {}
    # first pass: create a copy for every node
    curr = head
    while curr:
        mapping[curr] = Node(curr.val)
        curr = curr.next
    # second pass: wire next and random pointers
    curr = head
    while curr:
        copy = mapping[curr]
        if curr.next:
            copy.next = mapping[curr.next]
        else:
            copy.next = None
        if curr.random:
            copy.random = mapping[curr.random]
        else:
            copy.random = None
        curr = curr.next
    return mapping[head]
```

## Day 6 - Trees Foundation + BST

> All solutions assume `class TreeNode: def __init__(self, val=0, left=None, right=None): self.val, self.left, self.right = val, left, right`.

### 1. Maximum Depth Binary Tree (LC 104)

- Input: `root = [3,9,20,null,null,15,7]`
- Output: `3`
- Solution: DFS recursively returns `1 + max(left_depth, right_depth)`.
- Runtime Complexity: Time `O(n)`, Space `O(h)`

```python
def max_depth(root):
    if not root:
        return 0
    return 1 + max(max_depth(root.left), max_depth(root.right))
```

### 2. Binary Tree Level Order (LC 102)

- Input: `root = [3,9,20,null,null,15,7]`
- Output: `[[3],[9,20],[15,7]]`
- Solution: BFS with a queue. Process exactly the current queue length for each level.
- Runtime Complexity: Time `O(n)`, Space `O(w)` where `w` is max width

```python
from collections import deque

def level_order(root):
    if not root:
        return []
    result = []
    queue = deque([root])
    while queue:
        level = []
        for _ in range(len(queue)):
            node = queue.popleft()
            level.append(node.val)
            if node.left:
                queue.append(node.left)
            if node.right:
                queue.append(node.right)
        result.append(level)
    return result
```

### 3. Validate BST (LC 98)

- Input: `root = [2,1,3]`
- Output: `true`
- Solution: DFS with strict lower/upper bounds for each subtree.
- Runtime Complexity: Time `O(n)`, Space `O(h)`

```python
def is_valid_bst(root):
    def dfs(node, low, high):
        if not node:
            return True
        if not (low < node.val < high):
            return False
        return dfs(node.left, low, node.val) and dfs(node.right, node.val, high)
    return dfs(root, float('-inf'), float('inf'))
```

### 4. Kth Smallest in BST (LC 230)

- Input: `root = [3,1,4,null,2], k = 1`
- Output: `1`
- Solution: Iterative inorder traversal visits BST values in sorted order; stop at the kth visited node.
- Runtime Complexity: Time `O(h+k)`, Space `O(h)`

```python
def kth_smallest(root, k):
    stack = []
    node = root
    while stack or node:
        while node:
            stack.append(node)
            node = node.left
        node = stack.pop()
        k -= 1
        if k == 0:
            return node.val
        node = node.right
```

### 5. Diameter of Binary Tree (LC 543)

- Input: `root = [1,2,3,4,5]`
- Output: `3`
- Solution: DFS returns subtree depth while updating global best diameter as `left_depth + right_depth`.
- Runtime Complexity: Time `O(n)`, Space `O(h)`

```python
def diameter_of_binary_tree(root):
    best = 0
    def depth(node):
        nonlocal best
        if not node:
            return 0
        left = depth(node.left)
        right = depth(node.right)
        best = max(best, left + right)
        return 1 + max(left, right)
    depth(root)
    return best
```

### 6. LCA of BST (LC 235)

- Input: `root = [6,2,8,0,4,7,9,null,null,3,5], p = 2, q = 8`
- Output: `6`
- Solution: Use BST ordering. Move left if both targets are smaller, right if both are larger; otherwise current node is LCA.
- Runtime Complexity: Time `O(h)`, Space `O(1)`

```python
def lowest_common_ancestor_bst(root, p, q):
    node = root
    while node:
        if p.val < node.val and q.val < node.val:
            node = node.left
        elif p.val > node.val and q.val > node.val:
            node = node.right
        else:
            return node
```

### 7. Construct Tree from Preorder + Inorder (LC 105)

- Input: `preorder = [3,9,20,15,7], inorder = [9,3,15,20,7]`
- Output: `[3,9,20,null,null,15,7]`
- Solution: Preorder gives root order. Use an inorder index map to split left/right subtrees recursively.
- Runtime Complexity: Time `O(n)`, Space `O(n)`

```python
def build_tree(preorder, inorder):
    # map each value to its index in inorder for O(1) splits
    idx = {}
    for i in range(len(inorder)):
        idx[inorder[i]] = i
    pre_pos = [0]  # current index into preorder (list so inner func can mutate)

    def build(lo, hi):
        if lo > hi:
            return None
        val = preorder[pre_pos[0]]
        pre_pos[0] += 1
        node = TreeNode(val)
        mid = idx[val]
        node.left = build(lo, mid - 1)
        node.right = build(mid + 1, hi)
        return node

    return build(0, len(inorder) - 1)
```

## Day 7 - Trees Advanced + Trie

### 1. LCA of Binary Tree (LC 236)

- Input: `root = [3,5,1,6,2,0,8,null,null,7,4], p = 5, q = 1`
- Output: `3`
- Solution: DFS returns the node if it finds `p` or `q`; if left and right both return non-null, current node is LCA.
- Runtime Complexity: Time `O(n)`, Space `O(h)`

```python
def lowest_common_ancestor(root, p, q):
    if not root or root is p or root is q:
        return root
    left = lowest_common_ancestor(root.left, p, q)
    right = lowest_common_ancestor(root.right, p, q)
    if left and right:
        return root
    return left or right
```

### 2. Binary Tree Maximum Path Sum (LC 124)

- Input: `root = [-10,9,20,null,null,15,7]`
- Output: `42`
- Solution: DFS returns max one-sided gain. At each node, update global best using `node + left_gain + right_gain`.
- Runtime Complexity: Time `O(n)`, Space `O(h)`

```python
def max_path_sum(root):
    best = float('-inf')
    def gain(node):
        nonlocal best
        if not node:
            return 0
        left = max(gain(node.left), 0)
        right = max(gain(node.right), 0)
        best = max(best, node.val + left + right)
        return node.val + max(left, right)
    gain(root)
    return best
```

### 3. Binary Tree Right Side View (LC 199)

- Input: `root = [1,2,3,null,5,null,4]`
- Output: `[1,3,4]`
- Solution: BFS level order and record the last node of every level.
- Runtime Complexity: Time `O(n)`, Space `O(w)`

```python
from collections import deque

def right_side_view(root):
    if not root:
        return []
    result = []
    queue = deque([root])
    while queue:
        size = len(queue)
        for i in range(size):
            node = queue.popleft()
            if i == size - 1:
                result.append(node.val)
            if node.left:
                queue.append(node.left)
            if node.right:
                queue.append(node.right)
    return result
```

### 4. Serialize/Deserialize Binary Tree (LC 297)

- Input: `root = [1,2,3,null,null,4,5]`
- Output: `"1,2,3,#,#,4,5,#,#,#,#"` then original tree after deserialize
- Solution: Serialize with BFS and null markers. Deserialize by reading pairs of left/right children from the token stream.
- Runtime Complexity: Time `O(n)`, Space `O(n)`

```python
from collections import deque

class Codec:
    def serialize(self, root):
        out = []
        queue = deque([root])
        while queue:
            node = queue.popleft()
            if node:
                out.append(str(node.val))
                queue.append(node.left)
                queue.append(node.right)
            else:
                out.append('#')
        return ','.join(out)

    def deserialize(self, data):
        tokens = data.split(',')
        if tokens[0] == '#':
            return None
        root = TreeNode(int(tokens[0]))
        queue = deque([root])
        i = 1
        while queue:
            node = queue.popleft()
            if tokens[i] != '#':
                node.left = TreeNode(int(tokens[i]))
                queue.append(node.left)
            i += 1
            if tokens[i] != '#':
                node.right = TreeNode(int(tokens[i]))
                queue.append(node.right)
            i += 1
        return root
```

### 5. Implement Trie (LC 208)

- Input: `insert("apple"), search("apple"), startsWith("app")`
- Output: `true, true`
- Solution: Each trie node stores `children` and `is_end`. Insert/search by walking one character at a time.
- Runtime Complexity: Time `O(L)` per operation, Space `O(total inserted characters)`

```python
class Trie:
    def __init__(self):
        self.children = {}
        self.is_end = False

    def insert(self, word):
        node = self
        for ch in word:
            if ch not in node.children:
                node.children[ch] = Trie()
            node = node.children[ch]
        node.is_end = True

    def _find(self, word):
        node = self
        for ch in word:
            if ch not in node.children:
                return None
            node = node.children[ch]
        return node

    def search(self, word):
        node = self._find(word)
        return node is not None and node.is_end

    def startsWith(self, prefix):
        return self._find(prefix) is not None
```

### 6. Add and Search Words (LC 211)

- Input: `addWord("bad"), addWord("dad"), search("pad"), search(".ad")`
- Output: `false, true`
- Solution: Use a trie. For normal characters, follow one child; for `.`, DFS through all children.
- Runtime Complexity: Time `O(26^w)` worst case for `w` wildcards, Space `O(total inserted characters)`

```python
class WordDictionary:
    def __init__(self):
        self.children = {}
        self.is_end = False

    def addWord(self, word):
        node = self
        for ch in word:
            if ch not in node.children:
                node.children[ch] = WordDictionary()
            node = node.children[ch]
        node.is_end = True

    def search(self, word):
        def dfs(node, i):
            if i == len(word):
                return node.is_end
            ch = word[i]
            if ch == '.':
                # wildcard: try every child branch
                for key in node.children:
                    if dfs(node.children[key], i + 1):
                        return True
                return False
            if ch not in node.children:
                return False
            return dfs(node.children[ch], i + 1)
        return dfs(self, 0)
```

## Day 8 - Graphs

### 1. Number of Islands (LC 200)

- Input: `grid = [["1","1","0"],["1","0","0"],["0","0","1"]]`
- Output: `2`
- Solution: Scan the grid. When land is found, increment count and DFS/BFS to mark the whole island visited.
- Runtime Complexity: Time `O(R*C)`, Space `O(R*C)` worst case

```python
def num_islands(grid):
    if not grid:
        return 0
    rows, cols = len(grid), len(grid[0])
    count = 0
    def dfs(r, c):
        if r < 0 or r >= rows or c < 0 or c >= cols or grid[r][c] != '1':
            return
        grid[r][c] = '0'
        dfs(r + 1, c)
        dfs(r - 1, c)
        dfs(r, c + 1)
        dfs(r, c - 1)
    for r in range(rows):
        for c in range(cols):
            if grid[r][c] == '1':
                count += 1
                dfs(r, c)
    return count
```

### 2. Flood Fill (LC 733)

- Input: `image = [[1,1,1],[1,1,0],[1,0,1]], sr = 1, sc = 1, color = 2`
- Output: `[[2,2,2],[2,2,0],[2,0,1]]`
- Solution: DFS/BFS from the starting cell, changing connected cells with the original color to the new color.
- Runtime Complexity: Time `O(R*C)`, Space `O(R*C)` worst case

```python
def flood_fill(image, sr, sc, color):
    start = image[sr][sc]
    if start == color:
        return image
    rows, cols = len(image), len(image[0])
    def dfs(r, c):
        if r < 0 or r >= rows or c < 0 or c >= cols or image[r][c] != start:
            return
        image[r][c] = color
        dfs(r + 1, c)
        dfs(r - 1, c)
        dfs(r, c + 1)
        dfs(r, c - 1)
    dfs(sr, sc)
    return image
```

### 3. Rotting Oranges (LC 994)

- Input: `grid = [[2,1,1],[1,1,0],[0,1,1]]`
- Output: `4`
- Solution: Multi-source BFS from all rotten oranges. Each BFS layer represents one minute.
- Runtime Complexity: Time `O(R*C)`, Space `O(R*C)`

```python
from collections import deque

def oranges_rotting(grid):
    rows, cols = len(grid), len(grid[0])
    queue = deque()
    fresh = 0
    for r in range(rows):
        for c in range(cols):
            if grid[r][c] == 2:
                queue.append((r, c))
            elif grid[r][c] == 1:
                fresh += 1
    minutes = 0
    while queue and fresh:
        for _ in range(len(queue)):
            r, c = queue.popleft()
            for dr, dc in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nr, nc = r + dr, c + dc
                if 0 <= nr < rows and 0 <= nc < cols and grid[nr][nc] == 1:
                    grid[nr][nc] = 2
                    fresh -= 1
                    queue.append((nr, nc))
        minutes += 1
    return -1 if fresh else minutes
```

### 4. Clone Graph (LC 133)

- Input: `adjList = [[2,4],[1,3],[2,4],[1,3]]`
- Output: `deep copy of the graph`
- Solution: DFS/BFS with a hashmap from original node to copied node to avoid duplicate copies and cycles.
- Runtime Complexity: Time `O(V+E)`, Space `O(V)`

```python
def clone_graph(node):
    if not node:
        return None
    clones = {}
    def dfs(n):
        if n in clones:
            return clones[n]
        copy = Node(n.val)
        clones[n] = copy
        for neighbor in n.neighbors:
            copy.neighbors.append(dfs(neighbor))
        return copy
    return dfs(node)
```

### 5. Course Schedule I (LC 207)

- Input: `numCourses = 2, prerequisites = [[1,0]]`
- Output: `true`
- Solution: Build graph and indegrees. Kahn's topological sort succeeds if all courses are visited.
- Runtime Complexity: Time `O(V+E)`, Space `O(V+E)`

```python
from collections import deque

def can_finish(num_courses, prerequisites):
    # build adjacency list and indegree counts
    graph = {}
    for i in range(num_courses):
        graph[i] = []
    indeg = [0] * num_courses
    for pair in prerequisites:
        course = pair[0]
        pre = pair[1]
        graph[pre].append(course)
        indeg[course] += 1
    # start from every course that has no prerequisites
    queue = deque()
    for c in range(num_courses):
        if indeg[c] == 0:
            queue.append(c)
    visited = 0
    while len(queue) > 0:
        node = queue.popleft()
        visited += 1
        for nxt in graph[node]:
            indeg[nxt] -= 1
            if indeg[nxt] == 0:
                queue.append(nxt)
    return visited == num_courses
```

### 6. Course Schedule II (LC 210)

- Input: `numCourses = 4, prerequisites = [[1,0],[2,0],[3,1],[3,2]]`
- Output: `[0,1,2,3]` or `[0,2,1,3]`
- Solution: Kahn's topological sort, appending courses as their indegree becomes zero. Return empty list on cycle.
- Runtime Complexity: Time `O(V+E)`, Space `O(V+E)`

```python
from collections import deque

def find_order(num_courses, prerequisites):
    graph = {}
    for i in range(num_courses):
        graph[i] = []
    indeg = [0] * num_courses
    for pair in prerequisites:
        course = pair[0]
        pre = pair[1]
        graph[pre].append(course)
        indeg[course] += 1
    queue = deque()
    for c in range(num_courses):
        if indeg[c] == 0:
            queue.append(c)
    order = []
    while len(queue) > 0:
        node = queue.popleft()
        order.append(node)
        for nxt in graph[node]:
            indeg[nxt] -= 1
            if indeg[nxt] == 0:
                queue.append(nxt)
    if len(order) == num_courses:
        return order
    return []
```

### 7. Number of Connected Components (LC 323)

- Input: `n = 5, edges = [[0,1],[1,2],[3,4]]`
- Output: `2`
- Solution: Use Union-Find with path compression and union by rank; decrement component count on successful union.
- Runtime Complexity: Time `O((V+E)*alpha(V))`, Space `O(V)`

```python
def count_components(n, edges):
    parent = list(range(n))
    count = n
    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x
    def union(a, b):
        nonlocal count
        ra = find(a)
        rb = find(b)
        if ra != rb:
            parent[ra] = rb
            count -= 1
    for edge in edges:
        a = edge[0]
        b = edge[1]
        union(a, b)
    return count
```

### 8. Word Ladder (LC 127)

- Input: `beginWord = "hit", endWord = "cog", wordList = ["hot","dot","dog","lot","log","cog"]`
- Output: `5`
- Solution: BFS by changing one character at a time and visiting valid dictionary words once.
- Runtime Complexity: Time `O(N*L*26*L)` including string creation, Space `O(N)`

```python
from collections import deque

def ladder_length(begin_word, end_word, word_list):
    words = set(word_list)
    if end_word not in words:
        return 0
    queue = deque([(begin_word, 1)])
    while queue:
        word, steps = queue.popleft()
        if word == end_word:
            return steps
        for i in range(len(word)):
            for c in 'abcdefghijklmnopqrstuvwxyz':
                candidate = word[:i] + c + word[i + 1:]
                if candidate in words:
                    words.remove(candidate)
                    queue.append((candidate, steps + 1))
    return 0
```

## Day 9 - Heap + Intervals + Greedy

### 1. Kth Largest Element (LC 215)

- Input: `nums = [3,2,1,5,6,4], k = 2`
- Output: `5`
- Solution: Maintain a min-heap of size `k`; the heap root is the kth largest after all values are processed.
- Runtime Complexity: Time `O(n log k)`, Space `O(k)`

```python
import heapq

def find_kth_largest(nums, k):
    heap = []
    for num in nums:
        heapq.heappush(heap, num)
        if len(heap) > k:
            heapq.heappop(heap)
    return heap[0]
```

### 2. Top K Frequent Elements (LC 347)

- Input: `nums = [1,1,1,2,2,3], k = 2`
- Output: `[1,2]`
- Solution: Count frequencies, then use bucket sort by frequency and collect from highest bucket downward.
- Runtime Complexity: Time `O(n)`, Space `O(n)`

```python
def top_k_frequent(nums, k):
    # count how many times each number appears
    counts = {}
    for num in nums:
        if num in counts:
            counts[num] += 1
        else:
            counts[num] = 1
    # bucket index = frequency
    buckets = []
    for i in range(len(nums) + 1):
        buckets.append([])
    for num in counts:
        freq = counts[num]
        buckets[freq].append(num)
    # collect from the highest frequency downward
    result = []
    for freq in range(len(buckets) - 1, 0, -1):
        for num in buckets[freq]:
            result.append(num)
            if len(result) == k:
                return result
    return result
```

### 3. Find Median from Data Stream (LC 295)

- Input: `addNum(1), addNum(2), findMedian(), addNum(3), findMedian()`
- Output: `1.5, 2.0`
- Solution: Keep a max-heap for the lower half and a min-heap for the upper half; rebalance sizes after each insert.
- Runtime Complexity: Time `O(log n)` add, `O(1)` median, Space `O(n)`

```python
import heapq

class MedianFinder:
    def __init__(self):
        self.low = []   # max-heap (negated)
        self.high = []  # min-heap

    def addNum(self, num):
        heapq.heappush(self.low, -num)
        heapq.heappush(self.high, -heapq.heappop(self.low))
        if len(self.high) > len(self.low):
            heapq.heappush(self.low, -heapq.heappop(self.high))

    def findMedian(self):
        if len(self.low) > len(self.high):
            return -self.low[0]
        return (-self.low[0] + self.high[0]) / 2
```

### 4. Merge Intervals (LC 56)

- Input: `intervals = [[1,3],[2,6],[8,10],[15,18]]`
- Output: `[[1,6],[8,10],[15,18]]`
- Solution: Sort by start time. Merge into the previous interval when intervals overlap.
- Runtime Complexity: Time `O(n log n)`, Space `O(n)` for output

```python
def merge_intervals(intervals):
    intervals.sort()
    merged = []
    for interval in intervals:
        start = interval[0]
        end = interval[1]
        if len(merged) > 0 and start <= merged[-1][1]:
            merged[-1][1] = max(merged[-1][1], end)
        else:
            merged.append([start, end])
    return merged
```

### 5. Insert Interval (LC 57)

- Input: `intervals = [[1,3],[6,9]], newInterval = [2,5]`
- Output: `[[1,5],[6,9]]`
- Solution: Append intervals before the new interval, merge all overlaps, then append remaining intervals.
- Runtime Complexity: Time `O(n)`, Space `O(n)` for output

```python
def insert_interval(intervals, new_interval):
    result = []
    i, n = 0, len(intervals)
    while i < n and intervals[i][1] < new_interval[0]:
        result.append(intervals[i])
        i += 1
    while i < n and intervals[i][0] <= new_interval[1]:
        new_interval[0] = min(new_interval[0], intervals[i][0])
        new_interval[1] = max(new_interval[1], intervals[i][1])
        i += 1
    result.append(new_interval)
    result.extend(intervals[i:])
    return result
```

### 6. Meeting Rooms II (LC 253)

- Input: `intervals = [[0,30],[5,10],[15,20]]`
- Output: `2`
- Solution: Sort meetings by start time and keep a min-heap of active meeting end times.
- Runtime Complexity: Time `O(n log n)`, Space `O(n)`

```python
import heapq

def min_meeting_rooms(intervals):
    intervals.sort()
    heap = []  # active meeting end times
    for interval in intervals:
        start = interval[0]
        end = interval[1]
        # a room frees up if its meeting ended by this start time
        if len(heap) > 0 and heap[0] <= start:
            heapq.heappop(heap)
        heapq.heappush(heap, end)
    return len(heap)
```

### 7. Task Scheduler (LC 621)

- Input: `tasks = ["A","A","A","B","B","B"], n = 2`
- Output: `8`
- Solution: Use the greedy frame formula based on the highest task frequency and number of tasks tied for that frequency.
- Runtime Complexity: Time `O(T)`, Space `O(1)` because task labels are bounded

```python
def least_interval(tasks, n):
    # count how many times each task appears
    counts = {}
    for task in tasks:
        if task in counts:
            counts[task] += 1
        else:
            counts[task] = 1
    # find the highest frequency
    max_freq = 0
    for task in counts:
        max_freq = max(max_freq, counts[task])
    # count how many tasks share that highest frequency
    max_count = 0
    for task in counts:
        if counts[task] == max_freq:
            max_count += 1
    frame = (max_freq - 1) * (n + 1) + max_count
    return max(frame, len(tasks))
```

### 8. Maximum Matrix Sum (LC 1975)

- Input: `matrix = [[1,-1],[-1,1]]`
- Output: `4`
- Solution: Sum absolute values. If the number of negatives is odd, subtract twice the smallest absolute value.
- Runtime Complexity: Time `O(R*C)`, Space `O(1)`

```python
def max_matrix_sum(matrix):
    total = 0
    neg_count = 0
    smallest = float('inf')
    for row in matrix:
        for val in row:
            total += abs(val)
            smallest = min(smallest, abs(val))
            if val < 0:
                neg_count += 1
    if neg_count % 2 == 1:
        total -= 2 * smallest
    return total
```

## Day 10 - Dynamic Programming

### 1. Climbing Stairs (LC 70)

- Input: `n = 5`
- Output: `8`
- Solution: Fibonacci-style DP where `ways[i] = ways[i-1] + ways[i-2]`; keep only two variables.
- Runtime Complexity: Time `O(n)`, Space `O(1)`

```python
def climb_stairs(n):
    a = 1
    b = 1
    for i in range(n):
        temp = a + b
        a = b
        b = temp
    return a
```

### 2. House Robber I (LC 198)

- Input: `nums = [1,2,3,1]`
- Output: `4`
- Solution: Track best result when skipping or taking the current house.
- Runtime Complexity: Time `O(n)`, Space `O(1)`

```python
def rob(nums):
    prev = 0
    curr = 0
    for num in nums:
        # either skip this house (curr) or take it (prev + num)
        temp = max(curr, prev + num)
        prev = curr
        curr = temp
    return curr
```

### 3. House Robber II (LC 213)

- Input: `nums = [2,3,2]`
- Output: `3`
- Solution: Because houses are circular, solve two linear robberies: exclude first house or exclude last house.
- Runtime Complexity: Time `O(n)`, Space `O(1)`

```python
def rob_circular(nums):
    if len(nums) == 1:
        return nums[0]

    def rob_line(houses):
        prev = 0
        curr = 0
        for num in houses:
            temp = max(curr, prev + num)
            prev = curr
            curr = temp
        return curr

    # circular: either skip the first house or skip the last house
    return max(rob_line(nums[1:]), rob_line(nums[:-1]))
```

### 4. Coin Change (LC 322)

- Input: `coins = [1,2,5], amount = 11`
- Output: `3`
- Solution: Bottom-up DP where `dp[x]` is minimum coins to make amount `x`; relax with every coin.
- Runtime Complexity: Time `O(amount * coins)`, Space `O(amount)`

```python
def coin_change(coins, amount):
    dp = [float('inf')] * (amount + 1)
    dp[0] = 0
    for x in range(1, amount + 1):
        for coin in coins:
            if coin <= x:
                dp[x] = min(dp[x], dp[x - coin] + 1)
    return dp[amount] if dp[amount] != float('inf') else -1
```

### 5. Unique Paths (LC 62)

- Input: `m = 3, n = 7`
- Output: `28`
- Solution: 1D grid DP. Each cell's paths equal paths from top plus left.
- Runtime Complexity: Time `O(m*n)`, Space `O(n)`

```python
def unique_paths(m, n):
    row = [1] * n
    for _ in range(1, m):
        for c in range(1, n):
            row[c] += row[c - 1]
    return row[-1]
```

### 6. Longest Common Subsequence (LC 1143)

- Input: `text1 = "abcde", text2 = "ace"`
- Output: `3`
- Solution: 2D DP. If characters match, take diagonal plus one; otherwise take max of top/left.
- Runtime Complexity: Time `O(m*n)`, Space `O(m*n)`

```python
def longest_common_subsequence(text1, text2):
    m, n = len(text1), len(text2)
    # build an (m+1) x (n+1) grid of zeros
    dp = []
    for i in range(m + 1):
        row = [0] * (n + 1)
        dp.append(row)
    for i in range(1, m + 1):
        for j in range(1, n + 1):
            if text1[i - 1] == text2[j - 1]:
                dp[i][j] = dp[i - 1][j - 1] + 1
            else:
                dp[i][j] = max(dp[i - 1][j], dp[i][j - 1])
    return dp[m][n]
```

### 7. Word Break (LC 139)

- Input: `s = "leetcode", wordDict = ["leet","code"]`
- Output: `true`
- Solution: `dp[i]` is true if prefix `s[:i]` can be segmented. Try dictionary-length suffixes ending at `i`.
- Runtime Complexity: Time `O(n*L^2)` with slicing where `L` is max word length, Space `O(n + dictionary)`

```python
def word_break(s, word_dict):
    words = set(word_dict)
    dp = [False] * (len(s) + 1)
    dp[0] = True
    for i in range(1, len(s) + 1):
        for j in range(i):
            if dp[j] and s[j:i] in words:
                dp[i] = True
                break
    return dp[len(s)]
```

### 8. Jump Game (LC 55)

- Input: `nums = [2,3,1,1,4]`
- Output: `true`
- Solution: Greedily track farthest reachable index. If current index ever exceeds it, return false.
- Runtime Complexity: Time `O(n)`, Space `O(1)`

```python
def can_jump(nums):
    reach = 0
    for i in range(len(nums)):
        if i > reach:
            return False
        reach = max(reach, i + nums[i])
    return True
```

## Day 11 - Backtracking + Mock

### 1. Subsets (LC 78)

- Input: `nums = [1,2,3]`
- Output: `[[],[1],[2],[3],[1,2],[1,3],[2,3],[1,2,3]]`
- Solution: Backtrack from a start index; at each step append the current path and choose later elements.
- Runtime Complexity: Time `O(n*2^n)`, Space `O(n)` excluding output

```python
def subsets(nums):
    result = []
    def backtrack(start, path):
        result.append(path[:])
        for i in range(start, len(nums)):
            path.append(nums[i])
            backtrack(i + 1, path)
            path.pop()
    backtrack(0, [])
    return result
```

### 2. Permutations (LC 46)

- Input: `nums = [1,2,3]`
- Output: `[[1,2,3],[1,3,2],[2,1,3],[2,3,1],[3,1,2],[3,2,1]]`
- Solution: Backtrack with a `used` array; add a permutation when path length equals `n`.
- Runtime Complexity: Time `O(n*n!)`, Space `O(n)` excluding output

```python
def permute(nums):
    result = []
    used = [False] * len(nums)
    def backtrack(path):
        if len(path) == len(nums):
            result.append(path[:])
            return
        for i in range(len(nums)):
            if used[i]:
                continue
            used[i] = True
            path.append(nums[i])
            backtrack(path)
            path.pop()
            used[i] = False
    backtrack([])
    return result
```

### 3. Combination Sum (LC 39)

- Input: `candidates = [2,3,6,7], target = 7`
- Output: `[[2,2,3],[7]]`
- Solution: Sort candidates and backtrack with remaining target. Reuse the same index when a candidate may be used again.
- Runtime Complexity: Time exponential in search states, Space `O(target / min(candidate))` excluding output

```python
def combination_sum(candidates, target):
    candidates.sort()
    result = []
    def backtrack(start, remaining, path):
        if remaining == 0:
            result.append(path[:])
            return
        for i in range(start, len(candidates)):
            if candidates[i] > remaining:
                break
            path.append(candidates[i])
            backtrack(i, remaining - candidates[i], path)  # reuse i
            path.pop()
    backtrack(0, target, [])
    return result
```

### 4. Word Search (LC 79)

- Input: `board = [["A","B","C","E"],["S","F","C","S"],["A","D","E","E"]], word = "ABCCED"`
- Output: `true`
- Solution: DFS from each matching start cell, mark visited in-place during the path, and restore on backtrack.
- Runtime Complexity: Time `O(R*C*4^L)`, Space `O(L)`

```python
def exist(board, word):
    rows, cols = len(board), len(board[0])
    def dfs(r, c, i):
        if i == len(word):
            return True
        if r < 0 or r >= rows or c < 0 or c >= cols or board[r][c] != word[i]:
            return False
        board[r][c] = '#'  # mark visited
        found = (dfs(r + 1, c, i + 1) or dfs(r - 1, c, i + 1) or
                 dfs(r, c + 1, i + 1) or dfs(r, c - 1, i + 1))
        board[r][c] = word[i]  # restore
        return found
    for r in range(rows):
        for c in range(cols):
            if dfs(r, c, 0):
                return True
    return False
```

### 5. Generate Parentheses (LC 22)

- Input: `n = 3`
- Output: `["((()))","(()())","(())()","()(())","()()()"]`
- Solution: Backtrack with counts of open and close parentheses. Add `(` if open < n; add `)` if close < open.
- Runtime Complexity: Time `O(C_n * n)` where `C_n` is the nth Catalan number, Space `O(n)` excluding output

```python
def generate_parenthesis(n):
    result = []
    def backtrack(path, open_count, close_count):
        if len(path) == 2 * n:
            result.append(path)
            return
        # we can add '(' while we still have opens left
        if open_count < n:
            backtrack(path + '(', open_count + 1, close_count)
        # we can add ')' only if it has a matching '('
        if close_count < open_count:
            backtrack(path + ')', open_count, close_count + 1)
    backtrack("", 0, 0)
    return result
```

## Day 11 Mock Session Problems

### Mock 1. LRU Cache with Follow-ups

- Input: `Design get/put in O(1), then discuss thread-safety, TTL, singleton, and distributed scale.`
- Output: `Correct cache state after every operation and clear follow-up tradeoffs.`
- Solution: Use the Day 4 LRU design: hashmap plus doubly linked list. For follow-ups, add locks for thread safety, expiry metadata for TTL, and sharding/consistent hashing for scale.
- Runtime Complexity: Time `O(1)` average per get/put, Space `O(capacity)`

```python
import time

class DNodeTTL:
    def __init__(self, key, value, expire_at):
        self.key = key
        self.value = value
        self.expire_at = expire_at  # None => never expires
        self.prev = None
        self.next = None

class LRUCacheTTL:
    """O(1) LRU with optional per-key TTL (lazy expiry on access)."""
    def __init__(self, capacity):
        self.capacity = capacity
        self.cache = {}  # key -> DNodeTTL
        self.head = DNodeTTL(0, 0, None)
        self.tail = DNodeTTL(0, 0, None)
        self.head.next = self.tail
        self.tail.prev = self.head

    def _remove(self, node):
        node.prev.next = node.next
        node.next.prev = node.prev

    def _add_to_front(self, node):
        node.next = self.head.next
        node.prev = self.head
        self.head.next.prev = node
        self.head.next = node

    def get(self, key):
        if key not in self.cache:
            return -1
        node = self.cache[key]
        # lazily drop the entry if it has expired
        if node.expire_at is not None and time.time() > node.expire_at:
            self._remove(node)
            del self.cache[key]
            return -1
        self._remove(node)
        self._add_to_front(node)
        return node.value

    def put(self, key, value, ttl=None):
        expire_at = None
        if ttl:
            expire_at = time.time() + ttl
        if key in self.cache:
            old = self.cache[key]
            self._remove(old)
        node = DNodeTTL(key, value, expire_at)
        self.cache[key] = node
        self._add_to_front(node)
        if len(self.cache) > self.capacity:
            lru = self.tail.prev
            self._remove(lru)
            del self.cache[lru.key]

# Thread-safety: guard get/put with a lock (see Day 4 ThreadSafeLRU).
# Distributed scale: shard by consistent hashing across nodes;
# each node runs its own LRU; a router maps key -> node.
```

### Mock 2. Course Schedule II Variant

- Input: `numCourses = 4, prerequisites = [[1,0],[2,0],[3,1],[3,2]]`
- Output: `[0,1,2,3]` or `[0,2,1,3]`
- Solution: Use Kahn's topological sort. For streaming updates, maintain indegrees and adjacency incrementally, but re-check cycles after dependency changes.
- Runtime Complexity: Time `O(V+E)`, Space `O(V+E)`

```python
from collections import deque

def find_order(num_courses, prerequisites):
    graph = {}
    for i in range(num_courses):
        graph[i] = []
    indeg = [0] * num_courses
    for pair in prerequisites:
        course = pair[0]
        pre = pair[1]
        graph[pre].append(course)
        indeg[course] += 1
    queue = deque()
    for c in range(num_courses):
        if indeg[c] == 0:
            queue.append(c)
    order = []
    while len(queue) > 0:
        node = queue.popleft()
        order.append(node)
        for nxt in graph[node]:
            indeg[nxt] -= 1
            if indeg[nxt] == 0:
                queue.append(nxt)
    if len(order) == num_courses:
        return order
    return []  # empty => cycle
```

<!-- END GENERATED -->
