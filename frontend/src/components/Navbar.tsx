import { LogOut, BarChart2 } from 'lucide-react'
import { useAuth } from '../contexts/AuthContext'
import { colors } from '../theme/colors'

interface NavbarProps {
  username?: string | null
}

export default function Navbar({ username }: NavbarProps) {
  const { logout, userId } = useAuth()
  const label = username || (userId !== null ? `user #${userId}` : 'Account')

  return (
    <nav
      className="flex items-center justify-between px-6 py-4 border-b"
      style={{ background: colors.bg.secondary, borderColor: colors.border.default }}
    >
      <div className="flex items-center gap-2">
        <BarChart2 size={20} style={{ color: colors.accent.light }} />
        <span
          className="text-lg font-semibold tracking-tight"
          style={{ color: colors.text.primary }}
        >
          Tradeforces
        </span>
      </div>

      <div className="flex items-center gap-4">
        <span className="text-sm" style={{ color: colors.text.secondary }}>
          {label}
        </span>
        <button
          onClick={logout}
          className="flex items-center gap-2 rounded-lg border px-3 py-1.5 text-sm transition-colors duration-150"
          style={{ borderColor: colors.border.default, color: colors.text.primary }}
          onMouseEnter={(e) => (e.currentTarget.style.background = colors.bg.tertiary)}
          onMouseLeave={(e) => (e.currentTarget.style.background = 'transparent')}
        >
          <LogOut size={18} />
          Logout
        </button>
      </div>
    </nav>
  )
}
