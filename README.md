# Pullfrog AI Review

This repository uses **Pullfrog AI** to automatically review pull requests.

Pullfrog is an AI-powered code review agent that analyzes every PR for:
- Code quality and style issues
- Potential bugs and security vulnerabilities
- Performance bottlenecks
- Architecture and design concerns
- Best practice violations

When you open or update a pull request, Pullfrog will automatically run and leave
inline review comments on your code. You can also trigger it manually by commenting
`@pullfrog` on any PR.

The review is powered by OpenRouter, which provides access to leading LLMs.
All review results appear as PR comments and checks.
