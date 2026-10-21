namespace {

class AtomicPreferenceProbe : public UserPreferences
{
public:
	void setPath( const std::filesystem::path &path ) { m_filename = path.string().c_str(); }
};

static std::filesystem::path waterSettingsTestDirectory( const char *name )
{
	const std::filesystem::path root = std::filesystem::temp_directory_path() / "zh-water-settings-tests";
	std::filesystem::create_directories( root );
	const std::filesystem::path result = root / name;
	std::filesystem::remove_all( result );
	std::filesystem::create_directories( result );
	return result;
}

static std::string preferenceBytes( const std::filesystem::path &path )
{
	FILE *file = fopen( path.string().c_str(), "rb" );
	if( file == NULL )
		return std::string();
	fseek( file, 0, SEEK_END );
	const long size = ftell( file );
	fseek( file, 0, SEEK_SET );
	std::string result( size > 0 ? (size_t)size : 0, '\0' );
	if( !result.empty() )
		fread( &result[ 0 ], 1, result.size(), file );
	fclose( file );
	return result;
}

static Int preferenceTemporaryCount( const std::filesystem::path &directory )
{
	Int count = 0;
	for( std::filesystem::directory_iterator it( directory ), end; it != end; ++it )
		if( it->path().filename().string().find( "Options.ini.tmp." ) == 0 )
			++count;
	return count;
}

static AtomicPreferenceProbe preferenceProbe( const std::filesystem::path &path )
{
	AtomicPreferenceProbe pref;
	pref.setPath( path );
	pref[ AsciiString( "Alpha" ) ] = AsciiString( "one" );
	pref[ AsciiString( "WaterStyle" ) ] = AsciiString( "1" );
	return pref;
}

static Bool cleanupWaterSettingsTestDirectory( const std::filesystem::path &directory )
{
	std::error_code error;
	std::filesystem::remove_all( directory, error );
	if( error )
	{
		fprintf( stderr, "cleanup remove_all failed: %s (%d: %s)\n", directory.string().c_str(),
			error.value(), error.message().c_str() );
		return false;
	}
	std::filesystem::remove( directory.parent_path(), error );
	if( error && error != std::errc::directory_not_empty )
		fprintf( stderr, "cleanup remove failed: %s (%d: %s)\n", directory.parent_path().string().c_str(),
			error.value(), error.message().c_str() );
	return ( !error || error == std::errc::directory_not_empty ) && !std::filesystem::exists( directory );
}

}

TEST(user_preferences_atomic_write_preserves_existing_or_absent_destination_at_every_failure)
{
	const UserPreferencesWriteFault faults[] = {
		USER_PREFERENCES_WRITE_TEMP_OPEN,
		USER_PREFERENCES_WRITE_FORMATTED,
		USER_PREFERENCES_WRITE_SHORT,
		USER_PREFERENCES_WRITE_FLUSH,
		USER_PREFERENCES_WRITE_SYNC,
		USER_PREFERENCES_WRITE_CLOSE,
		USER_PREFERENCES_WRITE_REPLACE,
	};

	for( Int existing = 0; existing < 2; ++existing )
	{
		for( Int i = 0; i < (Int)( sizeof( faults ) / sizeof( faults[ 0 ] ) ); ++i )
		{
			const std::filesystem::path directory = waterSettingsTestDirectory( existing ? "failure-existing" : "failure-absent" );
			const std::filesystem::path path = directory / "Options.ini";
			if( existing )
				writeFile( path.string().c_str(), "Before = untouched\n" );
			AtomicPreferenceProbe pref = preferenceProbe( path );
			Set_User_Preferences_Write_Fault_For_Test( faults[ i ] );
			CHECK( !pref.write() );
			Set_User_Preferences_Write_Fault_For_Test( USER_PREFERENCES_WRITE_OK );
			CHECK_EQ( std::filesystem::exists( path ) ? 1 : 0, existing );
			if( existing )
				CHECK_STR( preferenceBytes( path ).c_str(), "Before = untouched\n" );
			CHECK_EQ( preferenceTemporaryCount( directory ), 0 );
			CHECK( cleanupWaterSettingsTestDirectory( directory ) );
		}
	}
}

TEST(user_preferences_atomic_write_commits_sorted_bytes_for_existing_and_absent_files)
{
	for( Int existing = 0; existing < 2; ++existing )
	{
		const std::filesystem::path directory = waterSettingsTestDirectory( existing ? "success-existing" : "success-absent" );
		const std::filesystem::path path = directory / "Options.ini";
		if( existing )
			writeFile( path.string().c_str(), "Before = replaced\n" );
		AtomicPreferenceProbe pref = preferenceProbe( path );
		CHECK( pref.write() );
		CHECK_STR( preferenceBytes( path ).c_str(), "Alpha = one\nWaterStyle = 1\n" );
		CHECK_EQ( preferenceTemporaryCount( directory ), 0 );
		CHECK( cleanupWaterSettingsTestDirectory( directory ) );
	}
}

TEST(water_style_commit_applies_only_after_the_candidate_file_is_committed)
{
	GlobalData *saved = TheWritableGlobalData;
	GlobalData *scratch = NEW GlobalData;
	TheWritableGlobalData = scratch;
	const std::filesystem::path directory = waterSettingsTestDirectory( "water-transaction" );
	const std::filesystem::path path = directory / "Options.ini";
	writeFile( path.string().c_str(), "WaterStyle = 1\n" );
	AtomicPreferenceProbe pref = preferenceProbe( path );

	CHECK_EQ( scratch->m_waterStyle, 1 );
	Set_User_Preferences_Write_Fault_For_Test( USER_PREFERENCES_WRITE_REPLACE );
	CHECK( !commitWaterStylePreference( pref, 0 ) );
	CHECK_EQ( scratch->m_waterStyle, 1 );
	CHECK_STR( preferenceBytes( path ).c_str(), "WaterStyle = 1\n" );
	CHECK_EQ( preferenceTemporaryCount( directory ), 0 );

	Set_User_Preferences_Write_Fault_For_Test( USER_PREFERENCES_WRITE_OK );
	CHECK( commitWaterStylePreference( pref, 0 ) );
	CHECK_EQ( scratch->m_waterStyle, 0 );
	CHECK_STR( preferenceBytes( path ).c_str(), "Alpha = one\nWaterStyle = 0\n" );

	delete scratch;
	TheWritableGlobalData = saved;
	CHECK( cleanupWaterSettingsTestDirectory( directory ) );
}

TEST(water_style_pending_choice_can_be_discarded_without_file_or_live_state_changes)
{
	GlobalData *saved = TheWritableGlobalData;
	GlobalData *scratch = NEW GlobalData;
	TheWritableGlobalData = scratch;
	const std::filesystem::path directory = waterSettingsTestDirectory( "water-cancel" );
	const std::filesystem::path path = directory / "Options.ini";
	writeFile( path.string().c_str(), "WaterStyle = 1\n" );
	AtomicPreferenceProbe pref = preferenceProbe( path );
	pref[ AsciiString( "WaterStyle" ) ] = AsciiString( "0" );

	CHECK_EQ( scratch->m_waterStyle, 1 );
	CHECK_STR( preferenceBytes( path ).c_str(), "WaterStyle = 1\n" );
	CHECK_EQ( preferenceTemporaryCount( directory ), 0 );

	delete scratch;
	TheWritableGlobalData = saved;
	CHECK( cleanupWaterSettingsTestDirectory( directory ) );
}

TEST(water_style_status_separates_selectability_production_readiness_and_effective_style)
{
	WaterRenderBridge::Capability core = {
		WaterRenderBridge::BackendKind::NativeD3D11,
		WaterRenderBridge::CoreRequired,
		WaterRenderBridge::CapabilityReason::None,
	};
	WaterStyleStatus status = MakeWaterStyleStatus( 1, 1, core );
	CHECK( status.isModernSelectable() );
	CHECK( !status.isProductionReady() );
	CHECK_EQ( status.requestedStyle, 1 );
	CHECK_EQ( status.effectiveStyle, 1 );

	WaterRenderBridge::Capability unavailable = {
		WaterRenderBridge::BackendKind::D3D9,
		0,
		WaterRenderBridge::CapabilityReason::UnsupportedBackend,
	};
	status = MakeWaterStyleStatus( 1, 0, unavailable );
	CHECK( !status.isModernSelectable() );
	CHECK( !status.isProductionReady() );
	CHECK_EQ( status.requestedStyle, 1 );
	CHECK_EQ( status.effectiveStyle, 0 );
	CHECK_EQ( WaterStyleRequestForOptions( status, 0 ), 0 );
	CHECK_EQ( WaterStyleRequestForOptions( status, 1 ), 1 );

	core.features = WaterRenderBridge::ProductionRequired;
	status = MakeWaterStyleStatus( 1, 1, core );
	CHECK( status.isModernSelectable() );
	CHECK( status.isProductionReady() );
	CHECK_EQ( WaterStyleRequestForOptions( status, 0 ), 1 );
}
