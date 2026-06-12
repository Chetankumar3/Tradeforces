import { Loader2, CheckCircle, XCircle } from 'lucide-react'
import type { UploadStatus } from '../types/upload'
import { colors } from '../theme/colors'

interface UploadProgressProps {
  status: UploadStatus
  zipProgress: number
  uploadProgress: number
  submissionId?: number | null
  errorMessage?: string
  onRetry?: () => void
}

export default function UploadProgress({
  status,
  zipProgress,
  uploadProgress,
  submissionId,
  errorMessage,
  onRetry,
}: UploadProgressProps) {
  if (status === 'idle') return null

  return (
    <div
      className="flex flex-col gap-3 rounded-xl border p-5"
      style={{ background: colors.bg.secondary, borderColor: colors.border.default }}
    >
      {status === 'zipping' && (
        <Row>
          <Loader2 size={20} className="animate-spin" style={{ color: colors.accent.light }} />
          <span style={{ color: colors.text.primary }}>
            Compressing folder… {zipProgress}%
          </span>
        </Row>
      )}

      {status === 'requesting' && (
        <Row>
          <Loader2 size={20} className="animate-spin" style={{ color: colors.accent.light }} />
          <span style={{ color: colors.text.primary }}>Preparing upload…</span>
        </Row>
      )}

      {status === 'uploading' && (
        <div className="flex flex-col gap-2">
          <Row>
            <Loader2
              size={20}
              className="animate-spin"
              style={{ color: colors.accent.light }}
            />
            <span style={{ color: colors.text.primary }}>
              Uploading… {uploadProgress}%
            </span>
          </Row>
          <ProgressBar percent={uploadProgress} />
        </div>
      )}

      {status === 'notifying' && (
        <Row>
          <Loader2 size={20} className="animate-spin" style={{ color: colors.accent.light }} />
          <span style={{ color: colors.text.primary }}>Finalising…</span>
        </Row>
      )}

      {status === 'done' && (
        <Row>
          <CheckCircle size={20} style={{ color: colors.status.success }} />
          <span style={{ color: colors.text.primary }}>
            Submission{' '}
            <span className="font-mono" style={{ color: colors.mono }}>
              #{submissionId}
            </span>{' '}
            received
          </span>
        </Row>
      )}

      {status === 'error' && (
        <div className="flex flex-col gap-3">
          <Row>
            <XCircle size={20} style={{ color: colors.status.error }} />
            <span style={{ color: colors.text.primary }}>
              {errorMessage || 'Upload failed'}
            </span>
          </Row>
          {onRetry && (
            <button
              onClick={onRetry}
              className="self-start rounded-lg px-4 py-2 text-sm font-medium transition-colors duration-150"
              style={{ background: colors.accent.primary, color: colors.text.inverse }}
              onMouseEnter={(e) =>
                (e.currentTarget.style.background = colors.accent.hover)
              }
              onMouseLeave={(e) =>
                (e.currentTarget.style.background = colors.accent.primary)
              }
            >
              Retry
            </button>
          )}
        </div>
      )}
    </div>
  )
}

function Row({ children }: { children: React.ReactNode }) {
  return <div className="flex items-center gap-3 text-sm">{children}</div>
}

function ProgressBar({ percent }: { percent: number }) {
  return (
    <div
      className="h-2 w-full overflow-hidden rounded-full"
      style={{ background: colors.bg.tertiary }}
    >
      <div
        className="h-full rounded-full transition-all duration-150"
        style={{ width: `${percent}%`, background: colors.accent.light }}
      />
    </div>
  )
}
