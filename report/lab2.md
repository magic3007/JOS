# Report for Lab 2

Operating System Engineering(Honor Track, 2019 Spring)

Jing Mai, 1700012751

------

# Git Operations
First of all, as I renew a private repository on my own github account, I have to fetch the branch `lab2` from `https://pdos.csail.mit.edu/6.828/2018/jos.git`.

```bash
git remote add mit https://pdos.csail.mit.edu/6.828/2018/jos.git
git remote -v
git fetch mit
git checkout -b lab2
git merge mit/lab2
git push --set-upstream origin lab2
````

# Part 1: Physical Page Management

As we had known in lab1, we set up a trivial page directory that translates virtual addresses `[KERNBASE, KERNBASE+4MB) `to physical addresses `[0, 4MB)`.  This 4MB region will be sufficient until we set up our real page table in `mem_init` in lab 2.



