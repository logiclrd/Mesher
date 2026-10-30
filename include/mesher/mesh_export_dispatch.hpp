#pragma once

#include "mesher/mesh_export_formats.hpp"
#include "mesher/mesh_export_transaction.hpp"

namespace mesher {

// Runs on the export writer, NEVER on the UI thread. Resolves one explicit
// dialog-selected encoding and loss policy to its corresponding serializer.
// No filename extension is consulted when selecting the format.
MeshExportEncoder meshExportEncoder(MeshExportPreset preset);

} // namespace mesher
