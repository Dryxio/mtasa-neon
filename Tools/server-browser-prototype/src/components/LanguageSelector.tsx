import { useEffect, useRef, useState } from 'react'
import languageIcon from '../../../../Shared/data/MTA San Andreas/MTA/cgui/images/the_language_icon.png'
import type { MenuLanguage } from '../menuBridge'
import { playUiSound } from '../uiSound'

interface LanguageSelectorProps {
  locale: string
  languages: readonly MenuLanguage[]
  onSelect: (locale: string) => void
}

export function LanguageSelector({ locale, languages, onSelect }: LanguageSelectorProps) {
  const [open, setOpen] = useState(false)
  const rootRef = useRef<HTMLDivElement>(null)
  const current = languages.find((language) => language.locale === locale) ?? languages[0]

  useEffect(() => {
    if (!open) return
    const close = (event: MouseEvent) => {
      if (rootRef.current && !rootRef.current.contains(event.target as Node)) setOpen(false)
    }
    window.addEventListener('mousedown', close)
    return () => window.removeEventListener('mousedown', close)
  }, [open])

  if (!current) return null

  return (
    <div className="language-selector" ref={rootRef}>
      {open && (
        <div className="language-selector__list" role="menu" aria-label="Choose language">
          {languages.map((language) => (
            <button
              key={language.locale}
              type="button"
              role="menuitemradio"
              aria-checked={language.locale === current.locale}
              className={`language-selector__option${language.locale === current.locale ? ' language-selector__option--selected' : ''}`}
              onMouseEnter={() => playUiSound('highlight')}
              onClick={() => {
                playUiSound('select')
                setOpen(false)
                onSelect(language.locale)
              }}
            >
              <span aria-hidden="true">▸</span>
              {language.name}
            </button>
          ))}
        </div>
      )}

      <button
        type="button"
        className="language-selector__button"
        aria-haspopup="menu"
        aria-expanded={open}
        onClick={() => {
          playUiSound('select')
          setOpen((value) => !value)
        }}
      >
        <img src={languageIcon} alt="" aria-hidden="true" />
        <span>{current.name}</span>
        <span className="language-selector__chevron" aria-hidden="true">▾</span>
      </button>
    </div>
  )
}
