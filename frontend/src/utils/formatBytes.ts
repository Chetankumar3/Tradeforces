export function formatBytes(n: number): string {
  if (!Number.isFinite(n) || n <= 0) return '0 B'
  const units = ['B', 'KB', 'MB', 'GB', 'TB']
  const i = Math.min(Math.floor(Math.log(n) / Math.log(1024)), units.length - 1)
  const value = n / Math.pow(1024, i)
  const formatted = i === 0 ? String(value) : value.toFixed(value >= 100 ? 0 : value >= 10 ? 1 : 2)
  return `${formatted} ${units[i]}`
}
