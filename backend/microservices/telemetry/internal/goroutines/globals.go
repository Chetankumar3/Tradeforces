// Package goroutines contains the 7 worker goroutines of the harness.
// All cross-goroutine communication happens over channels; no mutexes are used.
package goroutines

// debugEnabled is a package-level const (not var). When false, the compiler
// eliminates every `if debugEnabled { ... }` branch at compile time, so debug
// logging has zero runtime cost in production. Set to false and restart to disable.
const debugEnabled = true
