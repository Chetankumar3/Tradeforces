import { useEffect, useState } from 'react'
import { TrendingUp, AlertCircle, Zap, Clock, CheckCircle } from 'lucide-react'
import Navbar from '../components/Navbar'
import { colors } from '../theme/colors'

interface LeaderboardRow {
  rank: number
  team_id: string
  composite_score: number
  submission_id: string | null
  ack_p50_ns: number
  ack_p90_ns: number
  ack_p99_ns: number
  exec_p50_ns: number
  exec_p90_ns: number
  exec_p99_ns: number
  max_throughput_rps: number
  correctness_score: number
}

const DASH_PUSHER_URL = import.meta.env.VITE_DASHPUSHER_API_URL || 'http://localhost:8000'

function nsToMs(ns: number): string {
  if (!ns) return '—'
  return (ns / 1_000_000).toFixed(2) + ' ms'
}

function formatRps(rps: number): string {
  if (!rps) return '—'
  return rps.toLocaleString('en-US')
}

function formatScore(score: number): string {
  return score.toLocaleString('en-US', {
    minimumFractionDigits: 6,
    maximumFractionDigits: 6,
  })
}

function formatCorrectness(pct: number): string {
  if (!pct) return '—'
  return pct.toFixed(2) + '%'
}

function formatTime(date: Date | null): string {
  if (!date) return '—'
  return date.toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit', second: '2-digit' })
}

function getRankBadgeStyle(rank: number): { background: string; color: string } {
  if (rank === 1) return { background: '#FFD700', color: '#000' }
  if (rank === 2) return { background: '#C0C0C0', color: '#000' }
  if (rank === 3) return { background: '#CD7F32', color: '#000' }
  return { background: colors.accent.subtle, color: colors.accent.light }
}

// Column groups
const COLUMN_GROUPS = [
  {
    label: 'ACK Latency',
    icon: Clock,
    columns: [
      { key: 'ack_p50_ns', label: 'P50', format: nsToMs },
      { key: 'ack_p90_ns', label: 'P90', format: nsToMs },
      { key: 'ack_p99_ns', label: 'P99', format: nsToMs },
    ],
  },
  {
    label: 'Exec Latency',
    icon: Zap,
    columns: [
      { key: 'exec_p50_ns', label: 'P50', format: nsToMs },
      { key: 'exec_p90_ns', label: 'P90', format: nsToMs },
      { key: 'exec_p99_ns', label: 'P99', format: nsToMs },
    ],
  },
]

export default function LeaderboardPage() {
  const [leaderboard, setLeaderboard] = useState<LeaderboardRow[]>([])
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState<string | null>(null)
  const [lastUpdate, setLastUpdate] = useState<Date | null>(null)

  useEffect(() => {
    let eventSource: EventSource | null = null
    let reconnectTimeout: ReturnType<typeof setTimeout>

    const connect = () => {
      try {
        eventSource = new EventSource(`${DASH_PUSHER_URL}/leaderboard`)

        eventSource.addEventListener('leaderboard', (event) => {
          try {
            const data = JSON.parse(event.data)
            if (data.leaderboard && Array.isArray(data.leaderboard)) {
              setLeaderboard(data.leaderboard)
              setLastUpdate(new Date())
              setError(null)
              setLoading(false)
            }
          } catch {
            setError('Failed to parse leaderboard data')
          }
        })

        eventSource.onerror = () => {
          setError('Connection lost. Reconnecting...')
          eventSource?.close()
          reconnectTimeout = setTimeout(connect, 3000)
        }
      } catch {
        setError('Failed to connect to leaderboard service')
        setLoading(false)
      }
    }

    connect()
    return () => {
      eventSource?.close()
      clearTimeout(reconnectTimeout)
    }
  }, [])

  const thStyle: React.CSSProperties = {
    padding: '10px 14px',
    textAlign: 'right' as const,
    fontSize: '0.75rem',
    fontWeight: 600,
    color: colors.text.secondary,
    whiteSpace: 'nowrap' as const,
  }

  const thLeftStyle: React.CSSProperties = { ...thStyle, textAlign: 'left' }

  const tdStyle: React.CSSProperties = {
    padding: '12px 14px',
    textAlign: 'right' as const,
    fontSize: '0.8rem',
    color: colors.text.secondary,
    whiteSpace: 'nowrap' as const,
    borderBottom: `1px solid ${colors.border.default}`,
  }

  const tdLeftStyle: React.CSSProperties = { ...tdStyle, textAlign: 'left' }

  return (
    <div className="flex min-h-full flex-col" style={{ background: colors.bg.primary }}>
      <Navbar />

      <main className="mx-auto w-full max-w-screen-xl flex-1 px-4 py-8">
        {/* Header */}
        <div className="mb-6 flex items-center justify-between">
          <div className="flex items-center gap-3">
            <div
              className="flex h-10 w-10 items-center justify-center rounded-lg"
              style={{ background: colors.accent.subtle }}
            >
              <TrendingUp size={20} style={{ color: colors.accent.light }} />
            </div>
            <div>
              <h1 className="text-2xl font-bold" style={{ color: colors.text.primary }}>
                Leaderboard
              </h1>
              <p className="text-sm" style={{ color: colors.text.secondary }}>
                Live trading algorithm rankings
              </p>
            </div>
          </div>
          <div className="text-right">
            <p className="text-xs" style={{ color: colors.text.secondary }}>Last updated</p>
            <p className="text-sm font-medium" style={{ color: colors.text.primary }}>
              {formatTime(lastUpdate)}
            </p>
          </div>
        </div>

        {/* Error banner */}
        {error && (
          <div
            className="mb-4 flex items-center gap-2 rounded-lg border px-4 py-3"
            style={{ borderColor: colors.status.error, background: colors.bg.secondary }}
          >
            <AlertCircle size={16} style={{ color: colors.status.error }} />
            <span className="text-sm" style={{ color: colors.text.primary }}>{error}</span>
          </div>
        )}

        {/* Loading */}
        {loading && !leaderboard.length ? (
          <div
            className="flex flex-col items-center justify-center gap-3 rounded-xl border px-6 py-16"
            style={{ background: colors.bg.secondary, borderColor: colors.border.default }}
          >
            <div
              className="h-8 w-8 animate-spin rounded-full border-2 border-transparent"
              style={{ borderTopColor: colors.accent.light }}
            />
            <p style={{ color: colors.text.secondary }}>Connecting to leaderboard service...</p>
          </div>
        ) : (
          /* Scrollable table wrapper */
          <div
            className="overflow-x-auto rounded-xl border shadow"
            style={{ background: colors.bg.secondary, borderColor: colors.border.default }}
          >
            <table style={{ width: '100%', borderCollapse: 'collapse', minWidth: 1000 }}>
              <thead>
                {/* Group header row */}
                <tr style={{ borderBottom: `1px solid ${colors.border.default}` }}>
                  {/* Static columns span */}
                  <th colSpan={4} style={{ ...thLeftStyle, borderRight: `1px solid ${colors.border.default}` }} />

                  {COLUMN_GROUPS.map((group) => (
                    <th
                      key={group.label}
                      colSpan={group.columns.length}
                      style={{
                        ...thStyle,
                        textAlign: 'center',
                        borderRight: `1px solid ${colors.border.default}`,
                        paddingBottom: 6,
                      }}
                    >
                      <span className="flex items-center justify-center gap-1">
                        <group.icon size={12} />
                        {group.label}
                      </span>
                    </th>
                  ))}

                  {/* Throughput + Correctness + Score */}
                  <th colSpan={3} style={{ ...thStyle, textAlign: 'center' }} />
                </tr>

                {/* Column header row */}
                <tr style={{ borderBottom: `1px solid ${colors.border.default}` }}>
                  <th style={{ ...thLeftStyle, width: 60 }}>Rank</th>
                  <th style={{ ...thLeftStyle, minWidth: 100 }}>Team</th>
                  <th style={{ ...thLeftStyle, minWidth: 80 }}>Submission</th>
                  <th style={{ ...thStyle, minWidth: 110, borderRight: `1px solid ${colors.border.default}` }}>
                    Score
                  </th>

                  {COLUMN_GROUPS.map((group) =>
                    group.columns.map((col, i) => (
                      <th
                        key={col.key}
                        style={{
                          ...thStyle,
                          minWidth: 90,
                          ...(i === group.columns.length - 1
                            ? { borderRight: `1px solid ${colors.border.default}` }
                            : {}),
                        }}
                      >
                        {col.label}
                      </th>
                    ))
                  )}

                  <th style={{ ...thStyle, minWidth: 100 }}>
                    <span className="flex items-center justify-end gap-1">
                      <Zap size={11} /> Max Throughput
                    </span>
                  </th>
                  <th style={{ ...thStyle, minWidth: 100 }}>
                    <span className="flex items-center justify-end gap-1">
                      <CheckCircle size={11} /> Correctness
                    </span>
                  </th>
                </tr>
              </thead>

              <tbody>
                {leaderboard.length === 0 ? (
                  <tr>
                    <td colSpan={14} style={{ ...tdStyle, textAlign: 'center', padding: '2rem' }}>
                      No submissions yet
                    </td>
                  </tr>
                ) : (
                  leaderboard.map((row) => {
                    const badgeStyle = getRankBadgeStyle(row.rank)
                    return (
                      <tr
                        key={row.team_id}
                        onMouseEnter={(e) => { e.currentTarget.style.background = colors.accent.subtle }}
                        onMouseLeave={(e) => { e.currentTarget.style.background = 'transparent' }}
                        style={{ transition: 'background 120ms' }}
                      >
                        {/* Rank */}
                        <td style={tdLeftStyle}>
                          <span
                            style={{
                              ...badgeStyle,
                              display: 'inline-flex',
                              alignItems: 'center',
                              justifyContent: 'center',
                              width: 28,
                              height: 28,
                              borderRadius: '50%',
                              fontWeight: 700,
                              fontSize: '0.75rem',
                            }}
                          >
                            {row.rank}
                          </span>
                        </td>

                        {/* Team */}
                        <td style={{ ...tdLeftStyle, fontWeight: 600, color: colors.text.primary }}>
                          {row.team_id}
                        </td>

                        {/* Submission */}
                        <td style={tdLeftStyle}>
                          {row.submission_id ? `#${row.submission_id}` : '—'}
                        </td>

                        {/* Score */}
                        <td style={{ ...tdStyle, fontWeight: 700, color: colors.accent.light, borderRight: `1px solid ${colors.border.default}` }}>
                          {formatScore(row.composite_score)}
                        </td>

                        {/* ACK + Exec latency columns */}
                        {COLUMN_GROUPS.map((group) =>
                          group.columns.map((col, i) => (
                            <td
                              key={col.key}
                              style={{
                                ...tdStyle,
                                ...(i === group.columns.length - 1
                                  ? { borderRight: `1px solid ${colors.border.default}` }
                                  : {}),
                              }}
                            >
                              {col.format((row as any)[col.key])}
                            </td>
                          ))
                        )}

                        {/* Throughput */}
                        <td style={tdStyle}>{formatRps(row.max_throughput_rps)} rps</td>

                        {/* Correctness */}
                        <td style={{ ...tdStyle, color: row.correctness_score >= 90 ? colors.status.success : colors.text.secondary }}>
                          {formatCorrectness(row.correctness_score)}
                        </td>
                      </tr>
                    )
                  })
                )}
              </tbody>
            </table>
          </div>
        )}

        {/* Live indicator */}
        <div className="mt-5 flex items-center justify-center gap-2">
          <div
            className="h-2 w-2 rounded-full animate-pulse"
            style={{ background: colors.status.success }}
          />
          <p className="text-sm" style={{ color: colors.text.secondary }}>
            Live stream active
          </p>
        </div>
      </main>
    </div>
  )
}