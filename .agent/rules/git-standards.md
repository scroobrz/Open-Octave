---
trigger: always_on
description: Git version control and semantic commit standards for pair-programming.
---

# Git Version Control Standards for CodeCompanion

## 0. Core Mandate
*   **Version Control is NOT optional**: Every project created or modified must use Git.
*   **Commit Periodically**: As you code, commit logically after completing each component, feature, or fix. Do not wait until the end.
*   **Use Branches Appropriately**: Create feature branches for new features or significant refactoring. Keep `main` stable.
*   **Atomic Commits**: Each commit should represent a single logical change that passes the build.
*   **Semantic Messages**: Use Conventional Commits format for all commit messages.

---

## 1. Periodic Commits During Coding

### When to Commit

**Commit frequently as you work.** After completing any of the following, stage and commit your changes:

*   ✅ **After completing a component**: A class, function, service, or module
*   ✅ **After fixing a bug**: The fix is working and verified
*   ✅ **After adding a test**: New test file or test case is passing
*   ✅ **After refactoring**: Code structure improved, tests still pass
*   ✅ **After a configuration change**: Updated dependencies, build config, or environment files

### How to Commit

**❌ Don't do this:**
```bash
# ... code for 3 hours ...
# ... implement 5 features, fix 2 bugs, refactor 3 files ...
git add .
git commit -m "stuff"
```

**✅ Do this instead:**
```bash
# Complete feature 1
git add .                    # Stage all changes
# OR target specific files
git add src/Example.java       # Stage specific file
git commit -m "feat: add login form"

# Complete feature 2
git add .
git commit -m "feat: add password validation"

# Fix bug
git add .
git commit -m "fix: handle empty email input"

# Refactor
git add .
git commit -m "refactor: extract validation logic to helper"
```

---

## 2. Branching Strategy

### When to Create a Branch

**Create a new branch for:**
*   ✅ **Significant New Features**: Implementing new large-scale functionality, or any complex new features which will require multiple commits.
*   ✅ **Significant Refactoring**: Restructuring significant parts of the codebase, updating its overall architecture, or any complex refactoring which will require multiple commits.
*   ✅ **Bug Fixes**: Non-trivial bug fixes which will require multiple commits.
*   ✅ **Experiments**: Trying new approaches or libraries.

### Branch Naming Conventions

Use **kebab-case** with a descriptive prefix:

```bash
feat/add-user-login        # New feature
fix/email-validation       # Bug fix
refactor/user-service      # Refactoring
docs/api-guide             # Documentation
test/user-validation       # Adding tests
chore/update-deps          # Maintenance
```

**Be descriptive but concise**: `feat/add-payment-gateway` > `feat/payments` > `feat/bug1`

### Feature Branch Workflow

```bash
# 1. Checkout: Create and switch to feature branch
git checkout -b feat/add-payment-gateway

# 2. Code: Make changes and commit periodically
git add .
git commit -m "feat: add payment client interface"

# ... continue coding ...
git add .
git commit -m "feat: implement stripe payment client"

# ... continue coding ...
git add .
git commit -m "test: add payment gateway tests"

# 3. Sync: Ensure the branch is up-to-date before handing off
git checkout main
git pull origin main
git checkout feat/add-payment-gateway
git rebase main

# 4. Upload: Push the branch to the server
git push -u origin feat/add-payment-gateway

# 5. Hand-off: Return to main
git checkout main
```

### Branch Protection: Keep `main` Stable

*   **Never commit broken code to `main`**
*   **Test before merging**: Run tests on your feature branch first
*   **Review your changes**: Use `git diff main` to see what will be merged
*   **Merge when ready**: Use `git merge --no-ff` to preserve branch history

---

## 3. Semantic Commit Messages (REQUIRED)

Follow the [Conventional Commits](https://www.conventionalcommits.org/) specification for all commits.

### Format
```
<type>(<scope>): <subject>

[optional body]

[optional footer]
```

### Commit Types

| Type | Description | When to Use |
|------|-------------|-------------|
| `feat` | New feature | Adding new functionality |
| `fix` | Bug fix | Fixing a defect or issue |
| `docs` | Documentation | README, comments, Javadoc |
| `style` | Formatting | Code style, whitespace (no logic change) |
| `refactor` | Code restructuring | Improving code without changing behavior |
| `perf` | Performance | Optimizations, caching |
| `test` | Tests | Adding or updating tests |
| `chore` | Maintenance | Dependencies, build config |
| `build` | Build system | Maven, Gradle, npm scripts |
| `ci` | CI/CD | GitHub Actions, pipelines |

### Examples

```bash
# Feature
git commit -m "feat: add user authentication"
git commit -m "feat(auth): add password reset functionality"

# Bug fix
git commit -m "fix: resolve null pointer in login"
git commit -m "fix(api): handle empty response from user endpoint"

# Documentation
git commit -m "docs: update README with setup instructions"

# Refactoring
git commit -m "refactor: extract validation logic to service"

# Tests
git commit -m "test: add unit tests for UserService"

# Breaking change
git commit -m "feat(api)!: change response format to JSON:API

BREAKING CHANGE: API responses now follow JSON:API specification.
Clients must update their parsing logic."
```

### Commit Message Best Practices

*   ✅ **Use imperative mood**: "add" not "added", "fix" not "fixed"
*   ✅ **Be specific**: "fix: resolve email validation regex" > "fix: bug"
*   ✅ **Keep subject under 72 characters**
*   ✅ **Reference issues**: "fix: resolve login timeout (closes #42)"
*   ❌ **Don't be vague**: "updates", "stuff", "changes", "WIP"

---

## 4. Common Git Operations

### Viewing Status & History

```bash
# Check current state
git status

# View unstaged changes
git diff

# View staged changes
git diff --staged

# View commit history
git log --oneline -10         # Last 10 commits
git log --oneline <file>      # History of specific file
git log --graph --oneline     # Visual branch graph
```

### Staging & Committing

```bash
# Stage all changes
git add .

# Stage specific file
git add <file>

# Stage interactively (choose hunks)
git add -p

# Commit staged changes
git commit -m "type: message"

# Stage all tracked files and commit
git commit -am "type: message"
```

### Undoing Changes

```bash
# Unstage file (keep changes in working directory)
git restore --staged <file>

# Discard local changes (DESTRUCTIVE)
git restore <file>

# Amend last commit (change message or add files)
git commit --amend -m "new message"

# Amend without changing message
git commit --amend --no-edit

# Undo last commit (keep changes staged)
git reset --soft HEAD~1

# Undo last commit (keep changes unstaged)
git reset HEAD~1

# Undo last commit (DESTROY changes)
git reset --hard HEAD~1
```

### Stashing Work-in-Progress

Use stashing when you need to switch contexts without committing incomplete work.

```bash
# Stash current changes
git stash

# Stash with descriptive message
git stash save "WIP: implementing user service"

# List all stashes
git stash list

# Apply most recent stash (keep in stash list)
git stash apply

# Apply and remove most recent stash
git stash pop

# Apply specific stash
git stash apply stash@{2}

# Drop specific stash
git stash drop stash@{0}

# Clear all stashes
git stash clear
```

### Working with Remotes

```bash
# View configured remotes
git remote -v

# Fetch changes from remote (doesn't modify working directory)
git fetch origin

# Pull changes (fetch + merge)
git pull origin main

# Pull with rebase (cleaner history)
git pull --rebase origin main

# Push changes to remote
git push origin main

# Push new branch and set upstream
git push -u origin feat/new-feature

# Force push (use with caution, never on shared branches)
git push --force-with-lease origin feat/new-feature
```

---

## 5. .gitignore Essentials

**Always create a `.gitignore` before your first commit** to exclude build artifacts, dependencies, and secrets.

### By Language

```gitignore
# Java
target/
*.class
*.jar
*.war
.idea/
*.iml
.DS_Store

# Python
__pycache__/
*.pyc
*.pyo
.venv/
venv/
.env
*.egg-info/
dist/
build/

# JavaScript/TypeScript
node_modules/
dist/
build/
.next/
.cache/
.env
*.log

# Go
*.exe
*.test
vendor/
.env

# Rust
target/
Cargo.lock  # For libraries only, include for binaries
.env

# Universal
.DS_Store
*.log
.env
.env.local
*.swp
*.swo
*~
```

---

## 6. Best Practices Summary

### ✅ Do

*   **Commit early and often** (after each logical unit of work)
*   **Use branches for features and refactoring**
*   **Write descriptive, semantic commit messages**
*   **Review changes before committing** (`git diff`)
*   **Pull before pushing** (avoid conflicts)
*   **Use `.gitignore`** to exclude generated files and secrets
*   **Test before committing** (ensure build passes)

### ❌ Don't

*   **Commit secrets, API keys, or passwords** (use `.env` files in `.gitignore`)
*   **Use vague commit messages** ("fix stuff", "updates", "WIP")
*   **Commit commented-out code** (delete it, Git remembers)
*   **Commit large binary files** (use Git LFS if necessary)
*   **Force push to shared branches** (damages collaboration)
*   **Leave broken code in commits** (each commit should build successfully)
*   **Accumulate changes in one big commit** (commit incrementally)

---

## 7. Quick Reference

| Task | Command |
|------|---------|
| **Status & Info** | |
| Check status | `git status` |
| View unstaged changes | `git diff` |
| View staged changes | `git diff --staged` |
| View history | `git log --oneline -10` |
| **Staging & Committing** | |
| Stage all changes | `git add .` |
| Stage specific file | `git add <file>` |
| Commit | `git commit -m "type: message"` |
| Stage tracked + commit | `git commit -am "message"` |
| Amend last commit | `git commit --amend` |
| **Branching** | |
| Create branch | `git checkout -b <branch-name>` |
| Switch branch | `git checkout <branch-name>` |
| List branches | `git branch` |
| Delete branch | `git branch -d <branch-name>` |
| **Merging** | |
| Merge branch into current | `git merge <branch-name>` |
| Rebase current on branch | `git rebase <branch-name>` |
| **Remotes** | |
| View remotes | `git remote -v` |
| Fetch changes | `git fetch origin` |
| Pull changes | `git pull origin main` |
| Push changes | `git push origin main` |
| Push new branch | `git push -u origin <branch-name>` |
| **Stashing** | |
| Stash changes | `git stash` |
| List stashes | `git stash list` |
| Apply stash | `git stash pop` |
| **Undoing** | |
| Unstage file | `git restore --staged <file>` |
| Discard changes | `git restore <file>` |
| Undo last commit (keep changes) | `git reset --soft HEAD~1` |