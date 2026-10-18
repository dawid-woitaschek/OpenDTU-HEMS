// HEMS tariff-presentation wording overrides the inherited/base locale copy without duplicating the full locale files.
// These categories are deliberately presentation-only; optimizer and dispatch use exact numeric prices.
export const hemsTariffMessages = {
    en: {
        hemsUi: {
            cheap: 'Low',
            normal: 'Normal',
            expensive: 'High',
            nextcheap: 'Next low-price period',
            nextexpensive: 'Next high-price period',
            relativePrices: 'Price level by calendar day',
            classificationHint:
                'Low, normal and high describe relative price levels within each local calendar day; they are not control rules and do not guarantee savings. HEMS derives a duration-weighted robust p10–p90 price range and divides that price range into lower, middle and upper thirds. If the robust daily range is less than 5% of the day’s price scale, the whole day stays normal. Incomplete days use only available data. The optimizer and dispatch always use the exact numeric prices, never these display categories.',
        },
    },
    de: {
        hemsUi: {
            cheap: 'Niedrig',
            normal: 'Normal',
            expensive: 'Hoch',
            nextcheap: 'Nächster Zeitraum mit niedrigem Preisniveau',
            nextexpensive: 'Nächster Zeitraum mit hohem Preisniveau',
            relativePrices: 'Preisniveau je Kalendertag',
            classificationHint:
                'Niedrig, normal und hoch beschreiben nur das relative Preisniveau innerhalb des jeweiligen lokalen Kalendertags; sie sind keine Steuerregeln und garantieren keine Einsparung. HEMS bestimmt eine zeitgewichtete robuste p10–p90-Preisspanne und teilt diese Preisspanne in ein unteres, mittleres und oberes Drittel. Beträgt die robuste Tagesspanne weniger als 5 % des Preisniveaus, bleibt der gesamte Tag normal. Unvollständige Tage verwenden nur verfügbare Daten. Optimierer und Dispatch verwenden immer die exakten numerischen Preise und niemals diese Anzeigekategorien.',
        },
    },
    fr: {
        hemsUi: {
            cheap: 'Bas',
            normal: 'Normal',
            expensive: 'Élevé',
            nextcheap: 'Prochaine période à prix bas',
            nextexpensive: 'Prochaine période à prix élevé',
            relativePrices: 'Niveau de prix par jour civil',
            classificationHint:
                'Bas, normal et élevé décrivent uniquement les niveaux de prix relatifs de chaque jour civil local ; ce ne sont pas des règles de contrôle et ils ne garantissent aucune économie. HEMS calcule une plage de prix robuste p10–p90 pondérée par la durée et divise cette plage en tiers inférieur, médian et supérieur. Si la plage quotidienne robuste représente moins de 5 % du niveau de prix du jour, toute la journée reste normale. Les journées incomplètes utilisent uniquement les données disponibles. L’optimiseur et le dispatch utilisent toujours les prix numériques exacts, jamais ces catégories d’affichage.',
        },
    },
} as const;
