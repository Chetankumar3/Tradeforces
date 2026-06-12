import { useState } from 'react'
import { colors } from '../theme/colors'

interface TextFieldProps {
  label: string
  value: string
  onChange: (v: string) => void
  type?: string
  error?: string
  autoFocus?: boolean
  placeholder?: string
  autoComplete?: string
}

export default function TextField({
  label,
  value,
  onChange,
  type = 'text',
  error,
  autoFocus,
  placeholder,
  autoComplete,
}: TextFieldProps) {
  const [focused, setFocused] = useState(false)
  return (
    <label className="flex flex-col gap-1.5">
      <span className="text-sm font-medium" style={{ color: colors.text.accent }}>
        {label}
      </span>
      <input
        type={type}
        value={value}
        autoFocus={autoFocus}
        placeholder={placeholder}
        autoComplete={autoComplete}
        onChange={(e) => onChange(e.target.value)}
        onFocus={() => setFocused(true)}
        onBlur={() => setFocused(false)}
        className="rounded-lg border px-3 py-2 outline-none transition-colors duration-150"
        style={{
          background: colors.bg.tertiary,
          borderColor: error
            ? colors.status.error
            : focused
              ? colors.border.focus
              : colors.border.default,
          color: colors.text.primary,
          boxShadow: focused ? `0 0 0 2px ${colors.border.focus}55` : 'none',
        }}
      />
      {error && (
        <span className="text-xs" style={{ color: colors.status.error }}>
          {error}
        </span>
      )}
    </label>
  )
}
