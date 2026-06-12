import { useEffect } from 'react'
import { CheckCircle, XCircle, X } from 'lucide-react'
import { colors } from '../theme/colors'

interface StatusBannerProps {
  variant?: 'success' | 'error'
  message: string
  onDismiss: () => void
  autoDismissMs?: number
}

export default function StatusBanner({
  variant = 'success',
  message,
  onDismiss,
  autoDismissMs,
}: StatusBannerProps) {
  useEffect(() => {
    if (!autoDismissMs) return
    const t = setTimeout(onDismiss, autoDismissMs)
    return () => clearTimeout(t)
  }, [autoDismissMs, onDismiss])

  const tint = variant === 'success' ? colors.status.success : colors.status.error
  const Icon = variant === 'success' ? CheckCircle : XCircle

  return (
    <button
      onClick={onDismiss}
      className="flex w-full items-center gap-3 rounded-xl border px-4 py-3 text-left transition-colors duration-150"
      style={{
        background: colors.bg.secondary,
        borderColor: tint,
      }}
    >
      <Icon size={20} style={{ color: tint }} />
      <span className="flex-1 text-sm" style={{ color: colors.text.primary }}>
        {message}
      </span>
      <X size={18} style={{ color: colors.text.secondary }} />
    </button>
  )
}
