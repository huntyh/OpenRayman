#include <openrayman/game/ui_text.h>

namespace openrayman::game
{
	namespace
	{
		constexpr std::array<ui_translation, std::size_t(ui_key::count)> translations{{
			{{"Sound", "Son", "Ton", "Sonido", "Audio"}},
			{{"Display", "Affichage", "Anzeige", "Pantalla", "Schermo"}},
			{{"Game", "Jeu", "Spiel", "Juego", "Gioco"}},
			{{"Back", "Retour", "Zurück", "Volver", "Indietro"}},
			{{"Music", "Musique", "Musik", "Música", "Musica"}},
			{{"Effects", "Effets", "Effekte", "Efectos", "Effetti"}},
			{{"Screen", "Écran", "Bildschirm", "Pantalla", "Schermo"}},
			{{"Mode", "Mode", "Modus", "Modo", "Modo"}},
			{{"Vsync", "Vsync", "Vsync", "Vsync", "Vsync"}},
			{{"FPS limit", "Limite FPS", "FPS-Limit", "Límite FPS", "Limite FPS"}},
			{{"Loading", "Chargement", "Ladebilder", "Carga", "Caricamento"}},
			{{"Language", "Langue", "Sprache", "Idioma", "Lingua"}},
			{{"Autosave", "Sauvegarde auto", "Autospeichern", "Autoguardado", "Salvataggio auto"}},
			{{"Options", "Options", "Optionen", "Opciones", "Opzioni"}},
			{{"Options - Sound", "Options - Son", "Optionen - Ton", "Opciones - Sonido", "Opzioni - Audio"}},
			{{"Options - Display", "Options - Affichage", "Optionen - Anzeige", "Opciones - Pantalla", "Opzioni - Schermo"}},
			{{"Options - Game", "Options - Jeu", "Optionen - Spiel", "Opciones - Juego", "Opzioni - Gioco"}},
			{{"Windowed", "Normal", "Fenster", "Ventana", "Finestra"}},
			{{"Borderless", "Sans bords", "Randlos", "Sin bordes", "Senza bordi"}},
			{{"Exclusive", "Exclusif", "Exklusiv", "Exclusiva", "Esclusivo"}},
			{{"Desktop", "Bureau", "Desktop", "Escritorio", "Desktop"}},
			{{"On", "Oui", "Ein", "Sí", "Sì"}},
			{{"Strict", "Strict", "Strikt", "Estricto", "Rigido"}},
			{{"Off", "Non", "Aus", "No", "No"}},
			{{"No save", "Sans partie", "Kein Spiel", "Sin partida", "Nessuna partita"}},
			{{"Could not save settings", "Échec de sauvegarde", "Speichern fehlgeschlagen", "Error al guardar", "Salvataggio fallito"}},
			{{"Display change failed", "Échec de l'affichage", "Anzeigefehler", "Error de pantalla", "Errore schermo"}},
			{{"Yes", "Oui", "Ja", "Sí", "Sì"}},
			{{"No", "Non", "Nein", "No", "No"}},
			{{" already exists.", " existe déjà.", " ist schon vorhanden.", " ya existe.", " esiste già."}},
			{{"Load this saved game?", "Charger cette partie?", "Dieses Spiel laden?", "¿Cargar esta partida?", "Caricare questa partita?"}},
			{{"Delete this saved game?", "Effacer cette partie?", "Dieses Spiel löschen?", "¿Borrar esta partida?", "Eliminare questa partita?"}},
			{{"Save before leaving?", "Sauvegarder avant de quitter?", "Vor dem Verlassen speichern?", "¿Guardar antes de salir?", "Salvare prima di uscire?"}},
			{{"Abandon the current game?", "Abandonner la partie?", "Aktuelles Spiel verlassen?", "¿Abandonar la partida?", "Abbandonare la partita?"}},
			{{"Quit OpenRayman?", "Quitter OpenRayman?", "OpenRayman beenden?", "¿Salir de OpenRayman?", "Uscire da OpenRayman?"}},
			{{"Overwrite this saved game?", "Remplacer cette partie?", "Dieses Spiel überschreiben?", "¿Sobrescribir esta partida?", "Sovrascrivere la partita?"}},
			{{"Enable autosave?", "Sauvegarde auto?", "Automatisch speichern?", "¿Activar autoguardado?", "Salvataggio automatico?"}},
			{{"When this Lum is shown,", "Quand ce Lum est visible,", "Wenn dieser Lum erscheint,", "Cuando aparece este Lum,", "Quando appare questo Lum,"}},
			{{"the game is saving.", "la partie est sauvegardée.", "wird das Spiel gespeichert.", "se guarda la partida.", "la partita viene salvata."}},
			{{"Please do not turn off your system.", "N'éteignez pas votre appareil.", "Bitte das Gerät nicht ausschalten.", "No apagues el dispositivo.", "Non spegnere il dispositivo."}},
			{{"Press /O200:Enter/O-1: to continue.", "Appuyez sur /O200:Entrée/O-1: pour continuer.", "Zum Fortfahren /O200:Enter/O-1: drücken.", "Pulsa /O200:Intro/O-1: para continuar.", "Premi /O200:Invio/O-1: per continuare."}},
			{{"Saving", "Sauvegarde", "Speichern", "Guardando", "Salvataggio"}},
			{{"Type the game name or use the arrows./L:Press /O200:Enter/O-1: to start the game.",
			  "Tapez le nom ou utilisez les flèches./L:Appuyez sur /O200:Entrée/O-1: pour commencer.",
			  "Namen tippen oder Pfeile benutzen./L:Zum Starten /O200:Enter/O-1: drücken.",
			  "Escribe el nombre o usa las flechas./L:Pulsa /O200:Intro/O-1: para empezar.",
			  "Digita il nome o usa le frecce./L:Premi /O200:Invio/O-1: per iniziare."}},
			{{"English", "English", "English", "English", "English"}},
			{{"Français", "Français", "Français", "Français", "Français"}},
			{{"Español", "Español", "Español", "Español", "Español"}},
			{{"Italiano", "Italiano", "Italiano", "Italiano", "Italiano"}},
			{{"Deutsch", "Deutsch", "Deutsch", "Deutsch", "Deutsch"}}
		}};

		// The shipped font uses single-byte glyph codes, never UTF-8. Accented
		// lowercase letters have their own entries. Cedilla is composed at draw time.
		std::string encode(std::string_view text)
		{
			std::string result;
			for(std::size_t i = 0; i < text.size(); ++i) {
				auto byte = static_cast<unsigned char>(text[i]);
				if((byte == 0xC2 || byte == 0xC3) && i + 1 < text.size()) {
					byte = static_cast<unsigned char>(((byte & 0x1F) << 6) | (static_cast<unsigned char>(text[++i]) & 0x3F));
					// The menu font's accented E is in the lowercase byte slot.
					if(byte == 0xC9) byte = 0xE9;
				}
				result += char(byte);
			}
			return result;
		}
	}
	const std::array<ui_translation, std::size_t(ui_key::count)>& ui_translations() { return translations; }
	std::string ui_text(ui_key key, unsigned language)
	{
		constexpr unsigned columns[] = {0, 1, 3, 4, 2};
		return encode(translations.at(std::size_t(key))[columns[language < 5 ? language : 0]]);
	}
	std::string ui_language_name(unsigned language)
	{
		return ui_text(ui_key(std::size_t(ui_key::english_name) + (language < 5 ? language : 0)), 0);
	}
}
