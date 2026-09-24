import type { Metadata } from "next";
import "./globals.css";

export const metadata: Metadata = {
  title: "TRISHULA Mission Control",
  description: "Live lunar exploration mission control interface"
};

export default function RootLayout({ children }: Readonly<{ children: React.ReactNode }>) {
  return <html lang="en"><body>{children}</body></html>;
}
