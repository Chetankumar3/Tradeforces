import { useEffect, useState } from 'react'
import { TrendingUp, AlertCircle } from 'lucide-react'
import Navbar from '../components/Navbar'
import { colors } from '../theme/colors'

interface LeaderboardRow {
  rank: number
  team_id: string
  composite_score: number
  submission_id: string | null
}

const DASH_PUSHER_URL = import.meta.env.VITE_DASHPUSHER_API_URL || 'http://localhost:8000'

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
        console.log('Connecting to:', `${DASH_PUSHER_URL}/leaderboard`)
        eventSource = new EventSource(`${DASH_PUSHER_URL}/leaderboard`)

        eventSource.addEventListener('leaderboard', (event) => {
          console.log('Received leaderboard event:', event.data)
          try {
            const data = JSON.parse(event.data)
            console.log('Parsed data:', data)
            if (data.leaderboard && Array.isArray(data.leaderboard)) {
              console.log('Setting leaderboard:', data.leaderboard)
              setLeaderboard(data.leaderboard)
              setLastUpdate(new Date())
              setError(null)
              setLoading(false)
            }
          } catch (err) {
            console.error('Failed to parse leaderboard data:', err)
            setError('Failed to parse leaderboard data')
          }
        })

        eventSource.addEventListener('keep-alive', () => {
          console.log('Keep-alive received')
        })

        eventSource.onerror = (err) => {
          console.error('EventSource error:', err, eventSource?.readyState)
          setError('Connection lost. Reconnecting...')
          if (eventSource) {
            eventSource.close()
          }
          reconnectTimeout = setTimeout(() => {
            connect()
          }, 3000)
        }

        eventSource.onopen = () => {
          console.log('EventSource opened')
        }
      } catch (err) {
        console.error('Failed to connect:', err)
        setError('Failed to connect to leaderboard service')
        setLoading(false)
      }
    }

    connect()

    return () => {
      if (eventSource) {
        eventSource.close()
      }
      if (reconnectTimeout) {
        clearTimeout(reconnectTimeout)
      }
    }
  }, [])

  const formatScore = (score: number) => {
    return score.toLocaleString('en-US', {
      minimumFractionDigits: 2,
      maximumFractionDigits: 2,
    })
  }

  const formatTime = (date: Date | null) => {
    if (!date) return 'Loading...'
    return date.toLocaleTimeString('en-US', {
      hour: '2-digit',
      minute: '2-digit',
      second: '2-digit',
    })
  }

  const getRankColor = (rank: number) => {
    if (rank === 1) return '#FFD700' // Gold
    if (rank === 2) return '#C0C0C0' // Silver
    if (rank === 3) return '#CD7F32' // Bronze
    return colors.accent.light
  }

  return (
    <div className="flex min-h-full flex-col" style={{ background: colors.bg.primary }}>
      <Navbar />

      <main className="mx-auto w-full max-w-6xl flex-1 px-6 py-8">
        <div className="mb-8 flex items-center justify-between">
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
            <p className="text-xs" style={{ color: colors.text.secondary }}>
              Last updated
            </p>
            <p className="text-sm font-medium" style={{ color: colors.text.primary }}>
              {formatTime(lastUpdate)}
            </p>
          </div>
        </div>

        {error && (
          <div
            className="mb-6 flex items-center gap-2 rounded-lg border px-4 py-3"
            style={{ borderColor: colors.status.error, background: colors.bg.secondary }}
          >
            <AlertCircle size={18} style={{ color: colors.status.error }} />
            <span style={{ color: colors.text.primary }}>{error}</span>
          </div>
        )}

        {loading && !leaderboard.length ? (
          <div
            className="flex flex-col items-center justify-center gap-3 rounded-xl border px-6 py-12"
            style={{
              background: colors.bg.secondary,
              borderColor: colors.border.default,
            }}
          >
            <div className="h-8 w-8 animate-spin rounded-full border-2 border-transparent" style={{ borderTopColor: colors.accent.light }} />
            <p style={{ color: colors.text.secondary }}>
              Connecting to leaderboard service...
            </p>
          </div>
        ) : (
          <div
            className="overflow-hidden rounded-xl border shadow"
            style={{
              background: colors.bg.secondary,
              borderColor: colors.border.default,
            }}
          >
            <table className="w-full">
              <thead>
                <tr style={{ borderBottom: `1px solid ${colors.border.default}` }}>
                  <th
                    className="px-6 py-4 text-left text-sm font-semibold"
                    style={{ color: colors.text.secondary }}
                  >
                    Rank
                  </th>
                  <th
                    className="px-6 py-4 text-left text-sm font-semibold"
                    style={{ color: colors.text.secondary }}
                  >
                    Team ID
                  </th>
                  <th
                    className="px-6 py-4 text-left text-sm font-semibold"
                    style={{ color: colors.text.secondary }}
                  >
                    Composite Score
                  </th>
                  <th
                    className="px-6 py-4 text-left text-sm font-semibold"
                    style={{ color: colors.text.secondary }}
                  >
                    Latest Submission
                  </th>
                </tr>
              </thead>
              <tbody>
                {leaderboard.length === 0 ? (
                  <tr>
                    <td colSpan={4} className="px-6 py-8 text-center">
                      <p style={{ color: colors.text.secondary }}>
                        No submissions yet
                      </p>
                    </td>
                  </tr>
                ) : (
                  leaderboard.map((row) => (
                    <tr
                      key={row.team_id}
                      className="transition-colors duration-150 hover:bg-opacity-50"
                      style={{
                        borderBottom: `1px solid ${colors.border.default}`,
                      }}
                      onMouseEnter={(e) => {
                        e.currentTarget.style.background = colors.accent.subtle
                      }}
                      onMouseLeave={(e) => {
                        e.currentTarget.style.background = 'transparent'
                      }}
                    >
                      <td className="px-6 py-4">
                        <span
                          className="inline-flex h-8 w-8 items-center justify-center rounded-full font-bold text-sm"
                          style={{
                            background: getRankColor(row.rank),
                            color: row.rank <= 3 ? '#000' : colors.text.primary,
                          }}
                        >
                          {row.rank}
                        </span>
                      </td>
                      <td className="px-6 py-4">
                        <span className="font-medium" style={{ color: colors.text.primary }}>
                          {row.team_id}
                        </span>
                      </td>
                      <td className="px-6 py-4">
                        <span className="font-semibold" style={{ color: colors.accent.light }}>
                          {formatScore(row.composite_score)}
                        </span>
                      </td>
                      <td className="px-6 py-4">
                        <span style={{ color: colors.text.secondary }}>
                          {row.submission_id ? `#${row.submission_id}` : '—'}
                        </span>
                      </td>
                    </tr>
                  ))
                )}
              </tbody>
            </table>
          </div>
        )}

        <div className="mt-6 flex items-center justify-center gap-2">
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