#!/bin/bash
cd "$(dirname "$0")"
dotnet run --project userinterface/userinterface.csproj "$@"
