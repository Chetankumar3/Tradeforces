import { useEffect, useState } from 'react'
import { useLocation, useNavigate } from 'react-router-dom'
import { Upload, Clock, ChevronRight } from 'lucide-react'
import Navbar from '../components/Navbar'
import StatusBanner from '../components/StatusBanner'
import { colors } from '../theme/colors'

interface DashboardLocationState {
  lastSubmissionId?: number
}

export default function DashboardPage() {
  const navigate = useNavigate()
  const location = useLocation()
  const state = location.state as DashboardLocationState | null

  const [bannerId, setBannerId] = useState<number | null>(
    state?.lastSubmissionId ?? null,
  )

  // Clear the navigation state so the banner doesn't reappear on refresh/back.
  useEffect(() => {
    if (state?.lastSubmissionId != null) {
      navigate(location.pathname, { replace: true, state: null })
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [])

  return (
    <div className="flex min-h-full flex-col" style={{ background: colors.bg.primary }}>
      <Navbar />

      <main className="mx-auto w-full max-w-4xl flex-1 px-6 py-8">
        {bannerId != null && (
          <div className="mb-6">
            <StatusBanner
              variant="success"
              message={`Submission #${bannerId} queued for evaluation`}
              onDismiss={() => setBannerId(null)}
              autoDismissMs={6000}
            />
          </div>
        )}

        <h1 className="mb-6 text-2xl font-bold" style={{ color: colors.text.primary }}>
          Dashboard
        </h1>

        {/* New Submission card */}
        <button
          onClick={() => navigate('/upload')}
          className="group flex w-full items-center justify-between rounded-xl border p-6 text-left shadow transition-colors duration-150"
          style={{ background: colors.bg.secondary, borderColor: colors.border.default }}
          onMouseEnter={(e) => (e.currentTarget.style.borderColor = colors.accent.light)}
          onMouseLeave={(e) =>
            (e.currentTarget.style.borderColor = colors.border.default)
          }
        >
          <div className="flex items-center gap-4">
            <div
              className="flex h-12 w-12 items-center justify-center rounded-lg"
              style={{ background: colors.accent.subtle }}
            >
              <Upload size={20} style={{ color: colors.accent.light }} />
            </div>
            <div>
              <div className="font-semibold" style={{ color: colors.text.primary }}>
                New Submission
              </div>
              <div className="text-sm" style={{ color: colors.text.secondary }}>
                Upload a trading algorithm folder or .zip for evaluation.
              </div>
            </div>
          </div>
          <ChevronRight size={20} style={{ color: colors.text.secondary }} />
        </button>

        {/* Submission history placeholder */}
        <section className="mt-8">
          <h2
            className="mb-3 text-sm font-semibold uppercase tracking-wide"
            style={{ color: colors.text.secondary }}
          >
            Submission History
          </h2>
          <div
            className="flex flex-col items-center justify-center gap-3 rounded-xl border px-6 py-12 text-center"
            style={{
              background: colors.bg.secondary,
              borderColor: colors.border.default,
            }}
          >
            <Clock size={48} style={{ color: colors.text.secondary }} />
            <p className="text-sm" style={{ color: colors.text.secondary }}>
              Submission history coming soon
            </p>
          </div>
        </section>
      </main>
    </div>
  )
}
